# BlackTeaDisp
This project aims to create an opensource alternative Display for the [Blacktea Motorbikes Wildfire](https://www.blackteamotorbikes.com/pages/wildfire) electric 
motorcycle, to provide the rider more information than with the current simple display. The project uses the bluetooth-interface of the DalyBMS in the batterypacks
and the fardriver-motorcontroller and presents them on a slim wifi-webserver on an esp32 microcontroller

# Installation & Configuration
- Install VSCode / OSS and the platform.io Extension
- Clone/Download the Repository which contains the OSS project folder
- Open the projectfolder in VSCode ~~and edit the parameters in the `config.h` file~~ can now be done via webui
   - Note that for the DalyBMS you have to specify its mac_address but for the fardriver controller you have to give its Modelnumber instead,
     which you find in the fardriver-App, having 20 letters/numbers, starting with JSWX....
   - ~~Also edit `BMS_MAX_DEVS` to match the number of batteries you want to monitor~~ is set to 2, but you can disable them via webui
- Connect your ESP32 Board to your pc and go to the `pioarduino` Tab (Microchip-Symbol in the leftside toolbar in VSCode)
  - Build & upload the software by clicking `Upload` in the List in the left sidebar-panel
  - Also transfer the webserver-files for the gui by clicking `Upload Filesystem Image` in the List in the left sidebar-panel
- The ESP32 should restart (if not, remove/reattach its power to manually restart) and on your Phone you should see The wifi BTM Wildfire to which you can connect
  with your password set in `config.h`.
- Visit (http://192.168.4.1) to open the beautiful designed GUI, which might be further improved later ;)
- After a few seconds you should see some data appear, e.g. battery-soc, driving-mode and so on


# Planned Features
- [:hourglass:] support for second battery (should be working already but couldnt be tested due to lack of 2nd battery atm)
- [ ] more Datapoints and stats
- [:hourglass:] non-retard gui
- [ ] only send values via ws if they have changed (-> performance)
- [:hourglass:] allow to set/store the configuration via webinterface (-> generic builds & flash-script can be published, ease of use)
- [ ] installscript!
- [ ] AVAS/Engine-Sound via i2s to notify pedestrians and to signal that bike is on (-> prevent unwanted pull on gas)
- [ ] build an y-adaptercable, to get indicator, headlight & speedometer-signals to be shown on screen
