
/**
 *  NimBLE_Async_client Demo:
 *
 *  Demonstrates asynchronous client operations.
 *
 *  Created: on November 4, 2024
 *      Author: H2zero
 */

 #include <Arduino.h>
 #include <NimBLEDevice.h>
 
#define BT_DALY_CMD_SOC  0x90
#define BT_DALY_CMD_TRANGE 0x92


 static constexpr uint32_t scanTimeMs = 5 * 1000;
 boolean connected=false;
 NimBLERemoteService* rsvc;


 void notifyCb(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){
  Serial.printf("Hallo!");
};

void notifycb(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  std::string str  = (isNotify == true) ? "Notification" : "Indication";
  str             += " from ";
  str             += pRemoteCharacteristic->getClient()->getPeerAddress().toString();
  str             += ": Service = " + pRemoteCharacteristic->getRemoteService()->getUUID().toString();
  str             += ", Characteristic = " + pRemoteCharacteristic->getUUID().toString();
  str             += ", Value = " + std::string((char*)pData, length);
  Serial.printf("%s\n", str.c_str());
  Serial.printf("RAW-Data: ");
  int i;
  for (i = 0; i < length; i++){
    if (i > 0) printf(":");
    printf("%02X", pData[i]);
}
printf("\n");
    if(pData[2]==BT_DALY_CMD_SOC){
        uint16_t soc_perm=(uint16_t)((pData[10] &0xFF)<<8 | pData[11]) ;
        int64_t current_ma=((uint32_t)((pData[8] &0xFF)<<8 | pData[9])-30000)*100;
        uint32_t volt_tot_mv=(uint16_t)((pData[4] &0xFF)<<8 | pData[5])       *100;
        Serial.printf("SoC: %.1f %%, V_tot: %.1f V, current: %.1f A\n",soc_perm/10.0,volt_tot_mv/1000.0,current_ma/1000.0);
    }else if(pData[2]==BT_DALY_CMD_TRANGE){
        uint8_t highest_temp=pData[4] -40 ;
        uint8_t highest_temp_cell=pData[5] ;
        uint8_t lowest_temp=pData[6] -40 ;
        uint8_t lowest_temp_cell=pData[7] ;
        Serial.printf("highT(no. %u): %i °C, lowT(no. %u): %i °C", highest_temp_cell,highest_temp, lowest_temp_cell,lowest_temp);
    }

}

 class ClientCallbacks : public NimBLEClientCallbacks {
     void onConnect(NimBLEClient* pC) override {
         Serial.printf("Connected to: %s\n", pC->getPeerAddress().toString().c_str());
         
         connected=true;
     }
 
     void onDisconnect(NimBLEClient* pClient, int reason) override {
         Serial.printf("%s Disconnected, reason = %d - Starting scan\n", pClient->getPeerAddress().toString().c_str(), reason);
         NimBLEDevice::getScan()->start(scanTimeMs);
     }
 } clientCallbacks;
 
 class ScanCallbacks : public NimBLEScanCallbacks {
     void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
         Serial.printf("Advertised Device found: %s of type %u\n", advertisedDevice->toString().c_str(),advertisedDevice->getAddress().getType());
         if (advertisedDevice->haveName() && advertisedDevice->getAddress().toString() == "54:d2:72:36:6a:fc") {
             Serial.printf("Found Our Device of type\n ");

             /** Async connections can be made directly in the scan callbacks */
             auto pClient = NimBLEDevice::getDisconnectedClient();
             if (!pClient) {
                 pClient = NimBLEDevice::createClient(advertisedDevice->getAddress());
                 if (!pClient) {
                     Serial.printf("Failed to create client\n");
                     return;
                 }
             }
 
             pClient->setClientCallbacks(&clientCallbacks, false);
             if (!pClient->connect(true, true, false)) { // delete attributes, async connect, no MTU exchange
                 NimBLEDevice::deleteClient(pClient);
                 Serial.printf("Failed to connect\n");
                 return;
             }
         }
     }
 
     void onScanEnd(const NimBLEScanResults& results, int reason) override {
         Serial.printf("Scan Ended\n");
         NimBLEDevice::getScan()->start(scanTimeMs);
     }
 } scanCallbacks;
 



 void setup() {
     Serial.begin(115200);
     Serial.printf("Starting NimBLE Async Client\n");
     NimBLEDevice::init("Async-Client");
     NimBLEDevice::setPower(3); /** +3db */
 
     NimBLEScan* pScan = NimBLEDevice::getScan();
   //  pScan->setScanCallbacks(&scanCallbacks);
   //  pScan->setInterval(45);
   //  pScan->setWindow(45);
     pScan->setActiveScan(false);
    // pScan->start(scanTimeMs);
 }

 auto pClient = NimBLEDevice::getDisconnectedClient();




 void loop() {
     delay(100);
    if(connected){
        connected=false;

        const NimBLEUUID svc_uuid("0000fff0-0000-1000-8000-00805f9b34fb");
         Serial.printf("uuid: %s\n",svc_uuid.toString().c_str());
         
         Serial.printf("okili00\n");
         pClient->getServices(true); 
         pClient->discoverAttributes(); 
         rsvc =pClient->getService(svc_uuid);;
         Serial.printf("okili00\n");
        // pC->getServices(true);
         //if(pC){Serial.printf("notnulli\n");}
          
         Serial.printf("okili00\n");
         if(rsvc!=NULL){
            Serial.printf("Service found\n");
         }else{
            Serial.printf("Svc invalid\n");
         }
         rsvc->getCharacteristic(NimBLEUUID("0000fff1-0000-1000-8000-00805f9b34fb"))->subscribe(true,notifycb,true);
         Serial.printf("okili0\n");
         //soc,voltage,current
         const char cmd1[13]={0xa5,0x80,0x90,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbd};
         rsvc->getCharacteristic(NimBLEUUID("0000fff2-0000-1000-8000-00805f9b34fb"))->writeValue(cmd1,13);

         const char cmd2[13]={0xa5,0x80,0x92,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf};
         rsvc->getCharacteristic(NimBLEUUID("0000fff2-0000-1000-8000-00805f9b34fb"))->writeValue(cmd2,13);

         Serial.printf("okili1 \n");
        //  if(ch && ch->canWrite()){
        //     Serial.printf("okili\n");
        // }else{
        //     Serial.printf("NOT okili\n");
        // }



    }
     
     if (!pClient) {
      
      //pClient = NimBLEDevice::createClient(NimBLEAddress("64:E8:33:70:48:B2",BLE_ADDR_PUBLIC));
        pClient = NimBLEDevice::createClient(NimBLEAddress("40:18:03:01:23:cc",BLE_ADDR_RANDOM)); //verif
         if (!pClient) {
             Serial.printf("Failed to create client\n");
             return;
         }
     }

     if(!pClient->isConnected()){
      pClient->setClientCallbacks(&clientCallbacks, false);
      if (!pClient->connect(true, true, true)) { // delete attributes, async connect, no MTU exchange
        // NimBLEDevice::deleteClient(pClient);
         Serial.printf("Failed to connect\n");
         return;
      }else{
        //


      }

    }
    //Serial.printf("loop-di-love\n");

 }