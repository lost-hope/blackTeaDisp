
/**
 *  NimBLE_Async_client Demo:
 *
 *  Demonstrates asynchronous client operations.
 *
 *  Created: on November 4, 2024
 *      Author: H2zero
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"






#define BT_DALY_CMD_SOC  0x90
#define BT_DALY_CMD_VRANGE 0x91
#define BT_DALY_CMD_TRANGE 0x92

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

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

// struct BtDevice{
//     const String name;
//     uint32_t refresh_int_ms=200;
//     uint32_t last_refresh_ms=200;
//     NimBLEAddress address;
//     const String bt_id;
//     const BtType type;
//     bool enabled=true;
//     NimBLEClient* client;
// };

struct DalyBmsDevice{
    const String name;
    NimBLEClient* client;
    bool enabled=true;

    uint32_t refresh_int_ms=200;
    uint32_t last_refresh_ms=200;
    NimBLEAddress address;
};

struct FarDriverControllerDevice{
    const String name;
    NimBLEClient* client;
    bool enabled=true;

    uint8_t verified=0;
    const String bt_name;
    //the internal id  (e.g. JSWX....... ) of the controller; used instead of dynamic mac to identify this exact controller!
    char true_model_no[21];
    char recvd_model_no[21];
    NimBLEAddress wrong_addresses[20];
    uint8_t wa_index=0;
    unsigned long last_subscription=0;
};

uint8_t wheel_radius;
uint8_t wheel_width;
uint8_t wheel_ratio;
uint16_t rate_ratio;
uint16_t cur_speed_kph;

#include "config.h"

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
    strncpy(fardriver_controller_device.recvd_model_no,(char*)pData+(part==0?2:0),10);
    Serial.printf("expected serial: %s \n received serial: %s",fardriver_controller_device.true_model_no,fardriver_controller_device.recvd_model_no);
    if(strncmp(fardriver_controller_device.recvd_model_no+part*10,fardriver_controller_device.true_model_no+part*10,10)==0){
        fardriver_controller_device.verified|=(part+1);
    }else{
        fardriver_controller_device.verified=0;
        pRemoteCharacteristic->unsubscribe();
        fardriver_controller_device.client->disconnect();
    }
}

void notifycb_controller_fardriver(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    Serial.printf("-> Notify from Controller\n");
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
        //here we are sure that we have the right controller and can retrieve data
        if(pData[1]==0xaf){         //matches d0 !!!!
            wheel_ratio=  pData[6] ;
            wheel_radius= pData[7] ;
            alldata_doc["engine"]["avg_speed_kph"]= pData[8] ;
            wheel_width= pData[9] ;
            rate_ratio=(uint16_t)((pData[10] &0xFF)<<8 | pData[11]) ;
            alldata_doc["engine"]["cur_speed_kph"]=cur_speed_kph * (0.00376991136f * (wheel_radius * 1270.f + wheel_width * wheel_ratio) / rate_ratio);
        }else if(pData[1]==0xb0){   //matches 0xe2 !!!!
            cur_speed_kph=(uint16_t)((pData[8] &0xFF)<<8 | pData[9]) ;
        }else if(pData[1]==0xb5){   //matches 0xf4 ????

        }else if(pData[1]==0x8b){   //matches 0x1e ????

        }

        Serial.printf("maybe speed? @ %02X vals: %02X %02X", pData[1],pData[8],pData[9]);
    }

    


}

void notifycb_bms_daly(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    int bms_i=get_bms_index_by_client(pRemoteCharacteristic->getClient());

    if(bms_i==-1){
        Serial.printf("unknown notification source");
        return;
    }
  
    if(pData[2]==BT_DALY_CMD_SOC){
        uint16_t soc_perm=(uint16_t)((pData[10] &0xFF)<<8 | pData[11]) ;
        int64_t current_ma=((uint32_t)((pData[8] &0xFF)<<8 | pData[9])-30000)*100;
        uint32_t volt_tot_mv=(uint16_t)((pData[4] &0xFF)<<8 | pData[5])       *100;
        Serial.printf("SoC: %.1f %%, V_tot: %.1f V, current: %.1f A\n",soc_perm/10.0,volt_tot_mv/1000.0,current_ma/1000.0);
        alldata_doc["batteries"][bms_i]["soc_perm"]=soc_perm;
        alldata_doc["batteries"][bms_i]["current_ma"]=current_ma;
        alldata_doc["batteries"][bms_i]["volt_tot_mv"]=volt_tot_mv;
    }else if(pData[2]==BT_DALY_CMD_TRANGE){
        uint8_t highest_temp=pData[4] -40 ;
        uint8_t highest_temp_cell=pData[5] ;
        uint8_t lowest_temp=pData[6] -40 ;
        uint8_t lowest_temp_cell=pData[7] ;
        Serial.printf("highT(no. %u): %i °C, lowT(no. %u): %i °C\n", highest_temp_cell,highest_temp, lowest_temp_cell,lowest_temp);
        alldata_doc["batteries"][bms_i]["highest_temp"]=highest_temp;
        alldata_doc["batteries"][bms_i]["highest_temp_cell"]=highest_temp_cell;
        alldata_doc["batteries"][bms_i]["lowest_temp"]=lowest_temp;
        alldata_doc["batteries"][bms_i]["lowest_temp_cell"]=lowest_temp_cell;
    }else if(pData[2]==BT_DALY_CMD_VRANGE){
        uint16_t highest_v_mv=(uint16_t)((pData[4] &0xFF)<<8 | pData[5])  ;
        uint8_t highest_v_cell=pData[6] ;
        uint16_t lowest_v_mv=(uint16_t)((pData[7] &0xFF)<<8 | pData[8])  ;
        uint8_t lowest_v_cell=pData[9] ;
        Serial.printf("highV(no. %u): %i °mV, lowV(no. %u): %i mV\n", highest_v_cell,highest_v_mv, lowest_v_cell,lowest_v_mv);
        alldata_doc["batteries"][bms_i]["highest_v_mv"]=highest_v_mv;
        alldata_doc["batteries"][bms_i]["highest_v_cell"]=highest_v_cell;
        alldata_doc["batteries"][bms_i]["lowest_v_mv"]=lowest_v_mv;
        alldata_doc["batteries"][bms_i]["lowest_v_cell"]=lowest_v_cell;
    }else{
        Serial.printf("-> Notify from BMS\n");
        debug_notify(pRemoteCharacteristic, pData, length, isNotify);

    }

}

bool setup_fardriver_controller(){
    const NimBLEUUID svc_uuid("0000ffe0-0000-1000-8000-00805f9b34fb");
    //Serial.printf("uuid: %s\n",svc_uuid.toString().c_str());
    
    NimBLERemoteService* rsvc = fardriver_controller_device.client->getService(svc_uuid);
     
    if(rsvc==NULL){
       Serial.printf("Svc invalid\n");
       return false;
    }

    fardriver_controller_device.verified=0;
    fardriver_controller_device.recvd_model_no[20]='\0';
    rsvc->getCharacteristic(NimBLEUUID("0000ffec-0000-1000-8000-00805f9b34fb"))->subscribe(true,notifycb_controller_fardriver,true);
    
    return true;
}

bool setup_daly_bms(DalyBmsDevice* d){
    const NimBLEUUID svc_uuid("0000fff0-0000-1000-8000-00805f9b34fb");
    //Serial.printf("uuid: %s\n",svc_uuid.toString().c_str());
    
    NimBLERemoteService* rsvc =d->client->getService(svc_uuid);
     
    if(rsvc==NULL){
       Serial.printf("Svc invalid\n");
       return false;
    }
    rsvc->getCharacteristic(NimBLEUUID("0000fff1-0000-1000-8000-00805f9b34fb"))->subscribe(true,notifycb_bms_daly,true);
    
    //find details here:
    // - https://github.com/dreadnought/python-daly-bms/blob/main/dalybms/daly_bms.py
    // - https://github.com/dreadnought/python-daly-bms/issues/31

    //soc,voltage,current
    const char cmd1[13]={0xa5,0x80,BT_DALY_CMD_SOC,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbd};
    rsvc->getCharacteristic(NimBLEUUID("0000fff2-0000-1000-8000-00805f9b34fb"))->writeValue(cmd1,13);
    delay(50);
    //temp-range
    const char cmd2[13]={0xa5,0x80,BT_DALY_CMD_TRANGE,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf};
    rsvc->getCharacteristic(NimBLEUUID("0000fff2-0000-1000-8000-00805f9b34fb"))->writeValue(cmd2,13);
    delay(50);
    //voltage range
    const char cmd3[13]={0xa5,0x80,BT_DALY_CMD_VRANGE,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbe};
    rsvc->getCharacteristic(NimBLEUUID("0000fff2-0000-1000-8000-00805f9b34fb"))->writeValue(cmd3,13);
    delay(50);

    return true;

}
void initWiFi(){
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_password);
    WiFi.begin(private_ssid, private_password);
    IPAddress apIP = WiFi.softAPIP();
    IPAddress privateIP= WiFi.localIP();

    Serial.printf("AP IP: %s, Client IP: %s \n", apIP.toString().c_str(),privateIP.toString().c_str());
}

void setup_webserver_websocket(){
    server.on("/json2", HTTP_GET, [](AsyncWebServerRequest *request) {
        char json[1000];
        serializeJson(alldata_doc, json);
        request->send(200, "application/json", (const uint8_t *)json, strlen(json));
        //request->send(response);
    });
    server.begin();
}

void setup() {
    Serial.begin(115200);
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
        arr.add<JsonObject>();
    }

    fardriver_controller_device.client=NimBLEDevice::createClient();

}


void query_daly_bms(){
    for(int i=0;i<BMS_MAX_DEVS;i++){
        DalyBmsDevice* d=&daly_bms_devices[i];
        //Serial.printf("client null1: %i\n",d->client==NULL);
        
        if(!d->client->isConnected()){
            if(!d->client->connect(true, false, false)){
                Serial.printf("Failed to connect bms-client\n");
                continue;
            }
            
        }
        setup_daly_bms(d);
    }
}

void query_fardriver_controller(){
    if(!fardriver_controller_device.client->isConnected()){
        NimBLEScan *pScan = NimBLEDevice::getScan();
        NimBLEScanResults results = pScan->getResults(5 * 1000);
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice *device = results.getDevice(i);
                    
            if(strcmp(fardriver_controller_device.bt_name.c_str(),device->getName().c_str())!=0){
                continue;
            }
            //fardriver_controller_device.address=device->getAddress();
            if(!fardriver_controller_device.client->connect(device->getAddress(),true, false, false)){
                Serial.printf("Failed to connect controller-client\n");
                return;
            }
            
            
            break;
                    

        }
    }

    //renew subscription every 1-2s as a keepalive as the controller stops sending data after about 2.5s
    if(millis()-fardriver_controller_device.last_subscription>1800){
        fardriver_controller_device.last_subscription=millis();
        setup_fardriver_controller();
    }

}

void loop() {
     delay(100);

    //fardriver controller
    query_fardriver_controller();

    //daly bms systems
    query_daly_bms();
    //Serial.printf("loop-di-love\n");

 }