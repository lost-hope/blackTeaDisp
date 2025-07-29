#define BMS_MAX_DEVS 1

// Replace with your network credentials
const char* ap_ssid     = "BTM Wildfire";
const char* ap_password = "changeme";

const char* private_ssid     = "MY_PRIVATE_WIFI";
const char* private_password = "changeme";

//bt configs
DalyBmsDevice daly_bms_devices[BMS_MAX_DEVS]={
    {.name="right_battery",.enabled=true,.address=NimBLEAddress("XX:XX:XX:XX:XX:XX",BLE_ADDR_RANDOM)}
};

FarDriverControllerDevice fardriver_controller_device={.name="controller",.enabled=true,.bt_name="YuanQuFOC866",.true_model_no="JSWXxxxxxxxxxxxxxxxx"};
