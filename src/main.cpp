#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <regex.h>

#define BT_DALY_CMD_SOC  0x90
#define BT_DALY_CMD_VRANGE 0x91
#define BT_DALY_CMD_TRANGE 0x92
#define BT_DALY_CMD_PARAMS 0x50

#define IGNORE_LIST_SIZE 20

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// Store preferences and
Preferences persistentData;

//
JsonDocument alldata_doc;


// Initialize LittleFS
void initLittleFS() {
    if (!LittleFS.begin(true)) {
      Serial.println("An error has occurred while mounting LittleFS");
    }
    Serial.println("LittleFS mounted successfully");
}

enum BtType{
    BMS_DALY,
    CTRL_FARDRIVER
};

struct CtrlData{
    char recvd_model_no[21];
    uint8_t wheel_radius;
    uint8_t wheel_width;
    uint8_t wheel_ratio;
    uint16_t rate_ratio=1000;

    uint16_t cur_rpm;
    uint16_t cur_speed_kmh;
    uint16_t avg_speed_kmh;

    bool brake_switch;
    bool motion;
    bool sliding_backwards;
    uint8_t gear;

    int16_t engine_temp;
    int16_t controller_temp;
};

struct BmsData{
    uint32_t capacity_mah;

    uint16_t soc_perm;
    int64_t current_ma;
    uint32_t volt_tot_mv;

    uint8_t highest_temp;
    uint8_t highest_temp_cell;
    uint8_t lowest_temp;
    uint8_t lowest_temp_cell;

    uint16_t highest_v_mv;
    uint8_t highest_v_cell;
    uint16_t lowest_v_mv;
    uint8_t lowest_v_cell;
};

struct DalyBmsDevice{
    const String name;
    NimBLEClient* client;
    bool enabled=true;

    unsigned long slow_refresh_int_ms=60*1000;
    unsigned long last_slow_refresh_ms=0;
    unsigned long refresh_int_ms=2000;
    unsigned long last_refresh_ms=0;
    unsigned long fast_refresh_int_ms=200;
    unsigned long last_fast_refresh_ms=0;
    NimBLEAddress address;

    BmsData data;
};

uint64_t odometer;
String wifi_ap_ssid;
String wifi_ap_password;
String wifi_private_ssid;
String wifi_private_password;

struct FarDriverControllerDevice{
    const String name;
    NimBLEClient* client;
    bool enabled=true;

    uint8_t verified=0;
    const String bt_name;
    //the internal id  (e.g. JSWX....... ) of the controller; used instead of dynamic mac to identify this exact controller!
    char true_model_no[21];
   
    NimBLEAddress wrong_addresses[IGNORE_LIST_SIZE];
    uint8_t wa_index=0;
    unsigned long last_subscription=0;
    unsigned long last_scan=0;

    CtrlData data;
};


#include "config.h"

bool is_valid_mac_address(String fs){
    regex_t reegex;
    int v=regcomp( &reegex, "^([0-9a-f]{2}:){5}[0-9a-f]{2}$", REG_EXTENDED | REG_NOSUB);
    if(regexec(&reegex, fs.c_str(), 0, NULL, 0)!=0) {
        return false;  
    } 
    return true;
}

// /**
//  * @return true if values was changed, false if not
//  */
// template <typename T> bool update_if_changed(T* store,T new_val,JsonVariant json){
//     if((*store) != new_val){
//         (*store)=new_val;
//         json.set(new_val);
//         return true;
//     }
//     return false;
// }

void ws_send(JsonDocument& doc) {
    const size_t len = measureJson(doc);
  
    // original API from me-no-dev
    AsyncWebSocketMessageBuffer* buffer = ws.makeBuffer(len);
    assert(buffer); // up to you to keep or remove this
    serializeJson(doc, buffer->get(), len);
    ws.textAll(buffer);
}

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.printf("Connected to: %s\n", pClient->getPeerAddress().toString().c_str());
    }

    void onDisconnect(NimBLEClient* pClient, int reason) override {
        Serial.printf("%s Disconnected, reason = %d - Starting scan\n", pClient->getPeerAddress().toString().c_str(), reason);
    }
} clientCallbacks;


int8_t get_bms_index_by_client(NimBLEClient* c){
    for (int i = 0; i < BMS_MAX_DEVS; i++){
        if(daly_bms_devices[i].client == c ){
            return i;
        }
    }
    return -1;
}

void debug_notify(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){
    std::string str  = (isNotify == true) ? "Notification" : "Indication";
    str             += " from ";
    str             += pRemoteCharacteristic->getClient()->getPeerAddress().toString();
    str             += ": Service = " + pRemoteCharacteristic->getRemoteService()->getUUID().toString();
    str             += ", Characteristic = " + pRemoteCharacteristic->getUUID().toString();
    str             += ", Value = " + std::string((char*)pData, length);
    Serial.printf("%s\n", str.c_str());
    Serial.printf("RAW-Data: ");
    for (int i = 0; i < length; i++){
        if (i > 0) printf(":");
            Serial.printf("%02X", pData[i]);
    }
    Serial.printf("\n");
}


void verify_model_no(NimBLERemoteCharacteristic* pRemoteCharacteristic,uint8_t* pData, uint8_t part){
    if(part!=0 && part!=1)
        return;
    strncpy(fardriver_controller_device.data.recvd_model_no+part*10,(char*)pData+(part==0?4:2),10);
    Serial.printf("exp. serial: %s \nrec. serial: %s\n",fardriver_controller_device.true_model_no,fardriver_controller_device.data.recvd_model_no);
    if(strncmp(fardriver_controller_device.data.recvd_model_no+part*10,fardriver_controller_device.true_model_no+part*10,10)==0){
        fardriver_controller_device.verified|=(part+1);
        Serial.printf(" fardriver check for part %u passed! Verifxy is now at %u\n",part,fardriver_controller_device.verified);
    }else{
        Serial.printf(" fardriver check for part %u failed!\n",part);
        fardriver_controller_device.verified=0;
        pRemoteCharacteristic->unsubscribe();
        //add wrong address to ignore-list-ring
        fardriver_controller_device.wrong_addresses[fardriver_controller_device.wa_index]=fardriver_controller_device.client->getPeerAddress();
        fardriver_controller_device.wa_index=(fardriver_controller_device.wa_index+1)%IGNORE_LIST_SIZE;
        fardriver_controller_device.client->disconnect();
        
    }

    if(fardriver_controller_device.verified>=3){
        Serial.printf("We got the right fardriver device!");
    }
}

void notifycb_controller_fardriver(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    //Serial.printf("-> Notify from Controller\n");
    // credits to: https://github.com/jackhumbert/fardriver-controllers/blob/main/fardriver.hpp 
    //debug_notify(pRemoteCharacteristic, pData, length, isNotify);

    


    if(fardriver_controller_device.verified<3){
        //Check model number via notify @ address a1/a2
        if(pData[1]==0xa1){
            verify_model_no(pRemoteCharacteristic,pData,0);
        }else if(pData[1]==0xa2){
            verify_model_no(pRemoteCharacteristic,pData,1);
        }
    }else{
        JsonDocument doc;
        CtrlData* cd=&fardriver_controller_device.data;
        //here we are sure that we have the right controller and can retrieve data
        if(pData[1]==0xaf){         //matches d0 !!!!
            cd->wheel_ratio=  pData[6] ;
            cd->wheel_radius= pData[7] ;
            doc["controller"]["avg_speed_kph"]=cd->avg_speed_kmh= pData[8] ;
            cd->wheel_width= pData[9] ;
           //is not 4 or 4000 as expected, but 40975 - maybe that means sth. different or just need to be divided by 10?
           //changing the val in the app changes this value:
           // 1: 59935, 4: 40975, 5: 34835, 6: 28695, 8: 16415
           // rate_ratio=(uint16_t)((pData[10] &0xFF)<<8 | pData[11]) ;
            
           cd->rate_ratio =4000; //override for now
        }else if(pData[1]==0xb0){   //matches 0xe2 !!!!
            cd->cur_rpm=(uint16_t)((pData[9] &0xFF)<<8 | pData[8]) ;
                                                  //rpm     2*pi*60min/(100000) *(gesamtradius                              -->)
            doc["controller"]["cur_speed_kmh"]=cd->cur_speed_kmh=cd->cur_rpm * (0.00376991136f * (cd->wheel_radius * 1270.f + cd->wheel_width * cd->wheel_ratio) / cd->rate_ratio);
            //Serial.printf("wheelratio: %u, wheel_radius: %u, wheel_width: %u, rate_ratio: %u, rpm: %u\n",wheel_ratio,wheel_radius,wheel_width,rate_ratio,cur_rpm);
            //ws.textAll(String("{ \"speed\": "+String(alldata_doc["engine"]["cur_speed_kph"])+"}"));
            doc["controller"]["brake_switch"]=cd->brake_switch=(bool)(pData[5] & 0x80);
            doc["controller"]["gear"]=cd->gear=(uint8_t)(pData[2] & 0x0c)/4+1;
            doc["controller"]["sliding_backwards"]=cd->sliding_backwards=(bool)(pData[2] & 0x10);
            doc["controller"]["motion"]=cd->motion=(bool)(pData[2] & 0x20);

        }else if(pData[1]==0xb5){   //matches 0xf4 !!!!
            doc["controller"]["engine_temp"]=cd->engine_temp=(int16_t)((pData[3] &0xFF)<<8 | pData[2])  ;
        }else if(pData[1]==0x8b){   //matches 0x1e ????
            return;
        }else if(pData[1]==0xb3){   //matches 0xd6 ?! not that sure, but 0x86,0xb3 could also be a candidate
            //0xb3[12] gives a value that behaves like a temperature (getting smaller after stopping & cooling down)
            //0xb3[11] on the other hand alternates: going 0->1 every ~30-60s, than after 2s going back 1->0 this does
            //not look temperaturish. Maybe the mos-temp is only uint8_t in [12]?
            doc["controller"]["controller_temp"]=cd->controller_temp=(int16_t)(pData[12])  ;
        }else if(pData[1]==0x94){   //matches 0x69 !!!!
            return;
        }else{
            //return to prevent empty ws-messages
            return;
        }
        doc["other"]["time"]=millis();
        ws_send(doc);
        
        //alldata_doc["controller"]=doc["controller"];

    }

    


}
/**
 * @param path is of rfc6901 json pointer format
 */
template <typename T> void set_single(T* store, T val, String jsonpointer){
    if((*store)!=val){
        JsonDocument doc;
        (*store)=val;
        doc["ptr"]=jsonpointer;
        doc["val"]=val;
        ws_send(doc);
    }
}

void notifycb_bms_daly(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    int bms_i=get_bms_index_by_client(pRemoteCharacteristic->getClient());
    BmsData* bd=&daly_bms_devices[bms_i].data;
    if(bms_i==-1){
        Serial.printf("unknown notification source");
        return;
    }
    JsonDocument doc;
    
    if(pData[2]==BT_DALY_CMD_SOC){

        set_single<uint16_t>(&bd->soc_perm,(uint16_t)((pData[10] &0xFF)<<8 | pData[11]),String("/batteries/"+bms_i)+"/soc_perm");

        //update_if_changed<uint16_t>(&bd->soc_perm,(uint16_t)((pData[10] &0xFF)<<8 | pData[11]),doc["batteries"][bms_i]["soc_perm"]);
        doc["batteries"][bms_i]["current_ma"]=bd->current_ma=((int64_t)((pData[8] &0xFF)<<8 | pData[9])-30000)*100;
        doc["batteries"][bms_i]["volt_tot_mv"]=bd->volt_tot_mv=(uint16_t)((pData[4] &0xFF)<<8 | pData[5])       *100;
        Serial.printf("SoC: %.1f %%, V_tot: %.1f V, current: %.1f A\n",bd->soc_perm/10.0,bd->volt_tot_mv/1000.0,bd->current_ma/1000.0);
    }else if(pData[2]==BT_DALY_CMD_TRANGE){
        doc["batteries"][bms_i]["highest_temp"]=bd->highest_temp=pData[4] -40 ;
        doc["batteries"][bms_i]["highest_temp_cell"]=bd->highest_temp_cell=pData[5] ;
        doc["batteries"][bms_i]["lowest_temp"]=bd->lowest_temp=pData[6] -40 ;
        doc["batteries"][bms_i]["lowest_temp_cell"]=bd->lowest_temp_cell=pData[7] ;
        Serial.printf("highT(no. %u): %i °C, lowT(no. %u): %i °C\n", bd->highest_temp_cell,bd->highest_temp, bd->lowest_temp_cell,bd->lowest_temp);
    }else if(pData[2]==BT_DALY_CMD_VRANGE){
        doc["batteries"][bms_i]["highest_v_mv"]=bd->highest_v_mv=(uint16_t)((pData[4] &0xFF)<<8 | pData[5])  ;
        doc["batteries"][bms_i]["highest_v_cell"]=bd->highest_v_cell=pData[6] ;
        doc["batteries"][bms_i]["lowest_v_mv"]=bd->lowest_v_mv=(uint16_t)((pData[7] &0xFF)<<8 | pData[8])  ;
        doc["batteries"][bms_i]["lowest_v_cell"]=bd->lowest_v_cell=pData[9] ;
        Serial.printf("highV(no. %u): %i °mV, lowV(no. %u): %i mV\n", bd->highest_v_cell,bd->highest_v_mv, bd->lowest_v_cell,bd->lowest_v_mv);
    }else if(pData[2]==BT_DALY_CMD_PARAMS){
        //example: A501 5008 0000 C350 00000E102F
        doc["batteries"][bms_i]["capacity_mah"]=bd->capacity_mah=(uint32_t)((pData[4]<<24) | (pData[5] <<16) |(pData[6]<<8) | pData[7]) ;
    }else{
        Serial.printf("-> Notify from BMS\n");
        debug_notify(pRemoteCharacteristic, pData, length, isNotify);
        return;
    }

    // send ws-notify 
    ws_send(doc);
    //alldata_doc["batteries"][bms_i]=doc["batteries"][bms_i];
}

bool query_fardriver_controller(){
    const NimBLEUUID svc_uuid("0000ffe0-0000-1000-8000-00805f9b34fb");
    if(!fardriver_controller_device.client->isConnected()){
        return false;
    }
    //Serial.printf("uuid: %s\n",svc_uuid.toString().c_str());
    
    NimBLERemoteService* rsvc = fardriver_controller_device.client->getService(svc_uuid);
     
    if(rsvc==NULL){
       Serial.printf("Svc invalid\n");
       return false;
    }

    
    //fardriver_controller_device.recvd_model_no[20]='\0';
    rsvc->getCharacteristic(NimBLEUUID("0000ffec-0000-1000-8000-00805f9b34fb"))->subscribe(true,notifycb_controller_fardriver,true);
    
    return true;
}

bool query_daly_bms(){
    //find details here:
    // - https://github.com/dreadnought/python-daly-bms/blob/main/dalybms/daly_bms.py
    // - https://github.com/dreadnought/python-daly-bms/issues/31

    const NimBLEUUID svc_uuid("0000fff0-0000-1000-8000-00805f9b34fb");
    const NimBLEUUID rch_uuid("0000fff2-0000-1000-8000-00805f9b34fb");
    for(int i=0;i<BMS_MAX_DEVS;i++){
        DalyBmsDevice* d=&daly_bms_devices[i];
        
        if(!d->enabled || !d->client->isConnected()){
            continue;
        }

        NimBLERemoteService* rsvc =d->client->getService(svc_uuid);
     
        if(rsvc==NULL){
            Serial.printf("Svc invalid\n");
            continue;
        }
        rsvc->getCharacteristic(NimBLEUUID("0000fff1-0000-1000-8000-00805f9b34fb"))->subscribe(true,notifycb_bms_daly,true);


        //refresh values that change fast
        if(millis()-d->last_fast_refresh_ms > d->fast_refresh_int_ms){
            Serial.printf("Query BMS '%s'\n",d->name.c_str());
            d->last_fast_refresh_ms=millis();
            
            //soc,voltage,current
            delay(20);
            const char cmd1[13]={0xa5,0x80,BT_DALY_CMD_SOC,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbd};
            rsvc->getCharacteristic(rch_uuid)->writeValue(cmd1,13);
            

            //voltage range
            delay(20);
            const char cmd3[13]={0xa5,0x80,BT_DALY_CMD_VRANGE,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbe};
            rsvc->getCharacteristic(rch_uuid)->writeValue(cmd3,13);
            

        }

        //refresh values that only change seldom/slowly
        if(millis()-d->last_refresh_ms > d->refresh_int_ms){
            Serial.printf("Query BMS '%s'\n",d->name.c_str());
            d->last_refresh_ms=millis();
            
            //temp-range
            delay(30);
            const char cmd2[13]={0xa5,0x80,BT_DALY_CMD_TRANGE,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf};
            rsvc->getCharacteristic(rch_uuid)->writeValue(cmd2,13);
            
        }

        //refresh values that change almost never
        if(millis()-d->last_slow_refresh_ms > d->slow_refresh_int_ms){
            Serial.printf("Query BMS '%s'\n",d->name.c_str());
            d->last_slow_refresh_ms=millis();

            //params
            delay(50);
            const char cmd4[13]={0xa5,0x80,BT_DALY_CMD_PARAMS,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7d};
            rsvc->getCharacteristic(rch_uuid)->writeValue(cmd4,13);
            
        }
    }
    return true;

}
void initWiFi(){
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(wifi_ap_ssid, wifi_ap_password);
    if(wifi_private_ssid!=""){
        WiFi.begin(wifi_private_ssid, wifi_private_password);
    }
    IPAddress apIP = WiFi.softAPIP();
    IPAddress privateIP= WiFi.localIP();

    Serial.printf("AP IP: %s, Client IP: %s \n", apIP.toString().c_str(),privateIP.toString().c_str());
}

void savePersistentToNVS(){
    persistentData.putULong64("odometer", odometer);

    persistentData.putBool("ctrl_enabled", fardriver_controller_device.enabled);
    persistentData.putString("ctrl_model_no",fardriver_controller_device.true_model_no);

    for(int i=0;i<BMS_MAX_DEVS;i++){
        String akey="bat";
        akey.concat(i);
        String ekey=akey;
        akey.concat("_address");
        ekey.concat("_enabled");

        persistentData.putString(akey.c_str(),daly_bms_devices[i].address.toString().c_str());
        persistentData.putBool(ekey.c_str(),daly_bms_devices[i].enabled);
    }

}

void setup_webserver_websocket(){
    server.on("/json2", HTTP_GET, [](AsyncWebServerRequest *request) {

        for(int i=0;i<BMS_MAX_DEVS;i++){
            BmsData* bd=&daly_bms_devices[i].data;
            alldata_doc["batteries"][i]["enabled"]=daly_bms_devices[i].enabled;
            alldata_doc["batteries"][i]["connected"]=daly_bms_devices[i].client->isConnected();

            alldata_doc["batteries"][i]["soc_perm"]=bd->soc_perm;
            alldata_doc["batteries"][i]["current_ma"]=bd->current_ma;
            alldata_doc["batteries"][i]["volt_tot_mv"]=bd->volt_tot_mv;

            alldata_doc["batteries"][i]["highest_temp"]=bd->highest_temp;
            alldata_doc["batteries"][i]["highest_temp_cell"]=bd->highest_temp_cell;
            alldata_doc["batteries"][i]["lowest_temp"]=bd->lowest_temp;
            alldata_doc["batteries"][i]["lowest_temp_cell"]=bd->lowest_temp_cell;

            alldata_doc["batteries"][i]["highest_v_mv"]=bd->highest_v_mv;
            alldata_doc["batteries"][i]["highest_v_cell"]=bd->highest_v_cell;
            alldata_doc["batteries"][i]["lowest_v_mv"]=bd->lowest_v_mv;
            alldata_doc["batteries"][i]["lowest_v_cell"]=bd->lowest_v_cell;

            alldata_doc["batteries"][i]["capacity_mah"]=bd->capacity_mah;
        }
        CtrlData* cd=&fardriver_controller_device.data;

        alldata_doc["controller"]["enabled"]=fardriver_controller_device.enabled;
        alldata_doc["controller"]["connected"]=fardriver_controller_device.client->isConnected();

        alldata_doc["controller"]["model_no"]=cd->recvd_model_no;
        alldata_doc["controller"]["wheel_radius"]=cd->wheel_radius;
        alldata_doc["controller"]["wheel_width"]=cd->wheel_width;
        alldata_doc["controller"]["wheel_ratio"]=cd->wheel_ratio;
        alldata_doc["controller"]["rate_ratio"]=cd->rate_ratio;
    
        alldata_doc["controller"]["cur_rpm"]=cd->cur_rpm;
        alldata_doc["controller"]["cur_speed_kmh"]=cd->cur_speed_kmh;
        alldata_doc["controller"]["avg_speed_kmh"]=cd->avg_speed_kmh;
    
        alldata_doc["controller"]["brake_switch"]=cd->brake_switch;
        alldata_doc["controller"]["motion"]=cd->motion;
        alldata_doc["controller"]["sliding_backwards"]=cd->sliding_backwards;
        alldata_doc["controller"]["gear"]=cd->gear;

        alldata_doc["controller"]["engine_temp"]=cd->engine_temp;
        alldata_doc["controller"]["controller_temp"]=cd->controller_temp;





        char json[2000];
        serializeJsonPretty(alldata_doc, json);
        request->send(200, "application/json", (const uint8_t *)json, strlen(json));
        //request->send(response);
    });


    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        //request->send(LittleFS, "/gui/default/index.html", "text/html");
        request->redirect("gui/default/index.html");
      });

    server.on("/get_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();

        root["wifi_private_password"] = wifi_private_password;
        root["wifi_private_ssid"] = wifi_private_ssid;
    
        root["wifi_ap_password"] = wifi_ap_password;
        root["wifi_ap_ssid"] = wifi_ap_ssid;

        root["odometer"] = odometer;

        root["ctrl_serial"] = fardriver_controller_device.true_model_no;
        root["ctrl_enabled"] = true;

        JsonArray arr = root["batteries"].to<JsonArray>();
        for(int i=0;i<BMS_MAX_DEVS;i++){
            arr[i]["mac"] = daly_bms_devices[i].address.toString();
            arr[i]["enabled"] = daly_bms_devices[i].enabled;
        }


        serializeJson(root, *response);
        Serial.println();
        request->send(response);
    });

    server.on("/update_config",
            HTTP_POST,
            [](AsyncWebServerRequest * request){},
            NULL,
            [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
                for (size_t i = 0; i < len; i++) {
                    Serial.write(data[i]);
                }
                JsonDocument doc;
                deserializeJson(doc, data);
                
                //CHECK if exist else crash
                Serial.printf("bat0 mac %s\n",doc["bat0_mac"].as<const char*>());
                Serial.printf("bat0 en %s\n",doc["bat0_en"].as<const char*>());
              //Handling function implementation
              savePersistentToNVS();
          }
    );

    
    server.serveStatic("/gui", LittleFS, "/gui");
    server.addHandler(&ws);
    server.begin();
}



void loadPersistentFromNVS(){
    odometer=persistentData.getULong64("odometer");

    wifi_private_password = persistentData.getString("wifi_private_password");
    wifi_private_ssid = persistentData.getString("wifi_private_ssid");

    wifi_ap_password = persistentData.getString("wifi_ap_password");
    wifi_ap_ssid = persistentData.getString("wifi_ap_ssid");

    fardriver_controller_device.enabled=persistentData.getBool("ctrl_enabled");
    const char* m=persistentData.getString("ctrl_model_no").c_str();
    strncpy(fardriver_controller_device.true_model_no,m,21);

    for(int i=0;i<BMS_MAX_DEVS;i++){
        String akey="bat";
        akey.concat(i);
        String ekey=akey;
        akey.concat("_address");
        ekey.concat("_enabled");

        daly_bms_devices[i].address=NimBLEAddress(persistentData.getString(akey.c_str()).c_str(),BLE_ADDR_RANDOM);
        daly_bms_devices[i].enabled=persistentData.getBool(ekey.c_str());

    }
}

void initPersistent(){
    persistentData.begin("persistentData", false);   
    // open (or create and then open if it does not yet exist) in RW mode.
    if (persistentData.isKey("odo") == false) {
        //init values
        persistentData.putULong64("odometer", 0);

        persistentData.putString("wifi_private_password","");
        persistentData.putString("wifi_private_ssid","");
    
        persistentData.putString("wifi_ap_password","123456");
        persistentData.putString("wifi_ap_ssid","My BTM Wildfire");

        persistentData.putString("ctrl_model_no", "");
        persistentData.putBool("ctrl_enabled", false);
        for(int i=0;i<BMS_MAX_DEVS;i++){
            String akey="bat";
            akey.concat(i);
            String ekey=akey;
            akey.concat("_address");
            ekey.concat("_enabled");
            persistentData.putString(akey.c_str(), "");
            persistentData.putBool(ekey.c_str() ,false);
        }
    }
        
    loadPersistentFromNVS();
    
}

void setup() {
    Serial.begin(115200);
    initPersistent();
    initLittleFS();     
    initWiFi();
    setup_webserver_websocket();
  
    Serial.printf("Starting NimBLE Client\n");
    NimBLEDevice::init("BT-Client");
    NimBLEDevice::setPower(3); /** +3db */

    NimBLEScan* pScan = NimBLEDevice::getScan();
  //  pScan->setScanCallbacks(&scanCallbacks);
  //  pScan->setInterval(45);
  //  pScan->setWindow(45);
    pScan->setActiveScan(false);
   // pScan->start(scanTimeMs);


    //handle dalybms devices
    JsonArray arr = alldata_doc["batteries"].to<JsonArray>();
    
    
    for(int i=0;i<BMS_MAX_DEVS;i++){
        DalyBmsDevice* d=&daly_bms_devices[i];
        Serial.printf("Try to create bms-client for '%s' to %s\n",d->name.c_str(),d->address.toString().c_str());
        d->client=NimBLEDevice::createClient(d->address);
        if(!d->client){
            Serial.printf("Error creating client for bms %u\n",i);
        }
        Serial.printf("created.\n");
        arr.add<JsonObject>();
    }

    fardriver_controller_device.client=NimBLEDevice::createClient();

}


void connect_daly_bms(){
    for(int i=0;i<BMS_MAX_DEVS;i++){
        DalyBmsDevice* d=&daly_bms_devices[i];
        
        
        if(d->enabled && !d->client->isConnected()){
            d->client->setClientCallbacks(&clientCallbacks, false);
            Serial.printf("trying to connect to bms %u : %s\n",i, d->client->getPeerAddress().toString().c_str());
            if(!d->client->connect(true, true, false)){
                Serial.printf("Failed to connect bms-client\n");
                continue;
            }
            
        }
    }
}

bool is_ignored(const NimBLEAdvertisedDevice* dev){
    for(int i=0;i<IGNORE_LIST_SIZE;i++){
        if(fardriver_controller_device.wrong_addresses[i].equals(dev->getAddress())){
            return true;
        }
    }
    return false;
}

void connect_fardriver_controller(){
    if(!fardriver_controller_device.client->isConnected() && millis()-fardriver_controller_device.last_scan>10000){
        fardriver_controller_device.last_scan=millis();
        NimBLEScan *pScan = NimBLEDevice::getScan();
        //TODO: Async?
        NimBLEScanResults results = pScan->getResults(1 * 1000);
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice *device = results.getDevice(i);
                    
            if(strcmp(fardriver_controller_device.bt_name.c_str(),device->getName().c_str())!=0){
                continue;
            }
            //fardriver_controller_device.address=device->getAddress();
            //TODO: check if address was not falsified before and is on ignorelist
            if(is_ignored(device)){
                continue;
            }
            fardriver_controller_device.verified=0;
            fardriver_controller_device.client->setClientCallbacks(&clientCallbacks, false);
            if(!fardriver_controller_device.client->connect(device->getAddress(),true, true, false)){
                Serial.printf("Failed to connect controller-client\n");
                return;
            }
            
            
            break;
                    

        }
    }

    //renew subscription every 1-2s as a keepalive as the controller stops sending data after about 2.5s
    if(millis()-fardriver_controller_device.last_subscription>1800){
        Serial.printf("renew notify for controller\n");
        fardriver_controller_device.last_subscription=millis();
        query_fardriver_controller();
    }

}

int wstesttime=0;

void loop() {
    delay(10);

    //fardriver controller
    connect_fardriver_controller();
    query_fardriver_controller();

    //daly bms systems
    connect_daly_bms();
    query_daly_bms();

    //TODO: reconnect wifi-client if private wifi available


    if(millis()-wstesttime>500){
        wstesttime=millis();
        ws.textAll(String("{ \"time\": "+String(wstesttime)+"}"));
    }
    ws.cleanupClients();
 }