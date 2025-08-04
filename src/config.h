#define BMS_MAX_DEVS 1

// Replace with your network credentials
//const char* ap_ssid     = "My BTM Wildfire";
//const char* ap_password = "123456789";

//const char* private_ssid     = "FritzGuest";
//const char* private_password = "ghghgh12";

//bt configs
DalyBmsDevice daly_bms_devices[BMS_MAX_DEVS]={
    {.name="right_battery",.enabled=true,.address=NimBLEAddress("40:18:03:01:23:cc",BLE_ADDR_RANDOM)}
};

FarDriverControllerDevice fardriver_controller_device={.name="controller",.enabled=true,.bt_name="YuanQuFOC866",.true_model_no="JSWX0V250217IKGJ0060"};
