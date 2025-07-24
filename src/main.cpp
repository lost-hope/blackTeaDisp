
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





#define BT_MAX_DEVS 2
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


NimBLERemoteService* rsvc;
enum BtType{
    BMS_DALY,
    CTRL_FARDRIVER
};

struct BtDevice{
    const String name;
    uint32_t refresh_int_ms=200;
    uint32_t last_refresh_ms=200;
    NimBLEAddress address;
    const String bt_id;
    const BtType type;
    bool enabled=true;
    NimBLEClient* client;
};

#include "config.h"

void notifycb_bms_daly(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  
  
    if(pData[2]==BT_DALY_CMD_SOC){
        uint16_t soc_perm=(uint16_t)((pData[10] &0xFF)<<8 | pData[11]) ;
        int64_t current_ma=((uint32_t)((pData[8] &0xFF)<<8 | pData[9])-30000)*100;
        uint32_t volt_tot_mv=(uint16_t)((pData[4] &0xFF)<<8 | pData[5])       *100;
        Serial.printf("SoC: %.1f %%, V_tot: %.1f V, current: %.1f A\n",soc_perm/10.0,volt_tot_mv/1000.0,current_ma/1000.0);
        alldata_doc["batteries"][0]["soc_perm"]=soc_perm;
    }else if(pData[2]==BT_DALY_CMD_TRANGE){
        uint8_t highest_temp=pData[4] -40 ;
        uint8_t highest_temp_cell=pData[5] ;
        uint8_t lowest_temp=pData[6] -40 ;
        uint8_t lowest_temp_cell=pData[7] ;
        Serial.printf("highT(no. %u): %i °C, lowT(no. %u): %i °C\n", highest_temp_cell,highest_temp, lowest_temp_cell,lowest_temp);
    }else if(pData[2]==BT_DALY_CMD_VRANGE){
        uint16_t highest_v_mv=(uint16_t)((pData[4] &0xFF)<<8 | pData[5])  ;
        uint8_t highest_v_cell=pData[6] ;
        uint16_t lowest_v_mv=(uint16_t)((pData[7] &0xFF)<<8 | pData[8])  ;
        uint8_t lowest_v_cell=pData[9] ;
        Serial.printf("highV(no. %u): %i °mV, lowV(no. %u): %i mV\n", highest_v_cell,highest_v_mv, lowest_v_cell,lowest_v_mv);
    }else{
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
                printf("%02X", pData[i]);
        }
        printf("\n");

    }

}

bool setup_daly_bms(BtDevice* d){
   // BtDevice d=btdevices[index];
    const NimBLEUUID svc_uuid("0000fff0-0000-1000-8000-00805f9b34fb");
    //Serial.printf("uuid: %s\n",svc_uuid.toString().c_str());
    
    rsvc =d->client->getService(svc_uuid);;
    
    //if(pC){Serial.printf("notnulli\n");}
     
    if(rsvc==NULL){
       Serial.printf("Svc invalid\n");
       return false;
    }
    rsvc->getCharacteristic(NimBLEUUID("0000fff1-0000-1000-8000-00805f9b34fb"))->subscribe(true,notifycb_bms_daly,true);
    
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

    for(int i=0;i<BT_MAX_DEVS;i++){
        BtDevice* d=&btdevices[i];
        
        //handle fardriver seperately as its mac is dynamic and unknown
        if(!d->client && d->type!=CTRL_FARDRIVER){
            Serial.printf("Try to create client for '%s' to %s\n",d->name.c_str(),d->address.toString().c_str());
            d->client=NimBLEDevice::createClient(d->address);
        }
        //Serial.printf("client null2: %i\n",d->client==NULL);
    }
}

void loop() {
     delay(100);

    for(int i=0;i<BT_MAX_DEVS;i++){
        BtDevice* d=&btdevices[i];
        //Serial.printf("client null1: %i\n",d->client==NULL);
        
        if(!d->client->isConnected()){
            if(d->type!=CTRL_FARDRIVER){
                if(!d->client->connect(true, false, false)){
                    Serial.printf("Failed to connect client\n");
                    continue;
                }
            }else{
            //handle fardriver separately
                NimBLEScan *pScan = NimBLEDevice::getScan();
                NimBLEScanResults results = pScan->getResults(5 * 1000);
                for (int i = 0; i < results.getCount(); i++) {
                    const NimBLEAdvertisedDevice *device = results.getDevice(i);
                    
                    if(strcmp(d->bt_id.c_str(),device->getName().c_str())!=0){
                        continue;
                    }
                    d->address=device->getAddress();
                    if(!d->client->connect(true, false, false)){
                        Serial.printf("Failed to connect client\n");
                        continue;
                    }
                    //TODO: Check model number via notify @ address a1/a2
                    if(!modelnummer){
                        d->client->disconnect();
                        setcooldown ~5s
                        continue;
                    }
                }
            }
        }else{


        }
        //Serial.printf("connected.\n");

        //here we have connection
        if(d->type==BMS_DALY){
            setup_daly_bms(d);
        }else if(d->type==CTRL_FARDRIVER){
            //renew notify every 1.5-2.0s
        }


    }
    //Serial.printf("loop-di-love\n");

 }