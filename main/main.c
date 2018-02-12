/*
 * Copyleft (c) 2018 mfp20; https://github.com/mfp20
 *
 * esp-idf template with added features
 */

#include "templateplus.h"

/*
#include <M5Stack.h>
#include <Wire.h>
#include "EEPROM.h"
#include <M5StackSAM.h>

M5SAM MyMenu;

#define EEPROM_SIZE 64

void dummy(){
}
*/

void setup() {
/*
  M5.begin();
  M5.lcd.setBrightness(195);
  Serial.begin(115200);
  Wire.begin();

  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("failed to initialise EEPROM");
  }else{
    M5.lcd.setBrightness(byte(EEPROM.read(0)));
  }

  // CHANGING COLOR SCHEMA:
  //  MyMenu.setColorSchema(MENU_COLOR, WINDOW_COLOR, TEXT_COLO);
  //  COLORS are uint16_t (RGB565 format)
  // .MyMenu.getrgb(byte R, byte G, byte B); - CALCULATING RGB565 format

  //HERCULES MONITOR COLOR SCHEMA
  //MyMenu.setColorSchema(MyMenu.getrgb(0,0,0), MyMenu.getrgb(0,0,0), MyMenu.getrgb(0,255,0));

  // ADD MENU ITEM
  // MyMenu.addMenuItem(SUBMENU_ID,MENU_TITTLE,BTN_A_TITTLE,BTN_B_TITTLE,BTN_C_TITTLE,SELECTOR,FUNCTION_NAME);
  //    SUBMENU_ID byte [0-7]: TOP MENU = 0, SUBMENUs = [1-7]
  //    SELECTOR
  //           IF SELECTOR = -1 then MyMenu.execute() run function with name in last parameter (FUNCTION_NAME)
  //           IF SELECTOR = [0-7] then MyMenu.execute() switch menu items to SUBMENU_ID
  //    FUNCTION_NAME: name of function to run....

  MyMenu.addMenuItem(0,"APPLICATIONS","<","OK",">",1,dummy);
  MyMenu.addMenuItem(0,"SYSTEM","<","OK",">",2,dummy);
  MyMenu.addMenuItem(0,"CONFIGURATION","<","OK",">",3,dummy);
  MyMenu.addMenuItem(0,"ABOUT","<","OK",">",-1,dummy);

  MyMenu.addMenuItem(1,"WiFi SCANNER","<","OK",">",-1,appWiFiScanner);
  MyMenu.addMenuItem(1,"I2C SCANNER","<","OK",">",-1,appIICScanner);
  MyMenu.addMenuItem(1,"STOPWATCH","<","OK",">",-1,appStopWatch);
  MyMenu.addMenuItem(1,"RETURN","<","OK",">",0,dummy);

  MyMenu.addMenuItem(2,"SYSTEM INFORMATIONS","<","OK",">",-1,appSysInfo);
  MyMenu.addMenuItem(2,"SLEEP/CHARGING","<","OK",">",-1,appSleep);
  MyMenu.addMenuItem(2,"RETURN","<","OK",">",0,dummy);

  MyMenu.addMenuItem(3,"DISPLAY BACKLIGHT","<","OK",">",-1,appCfgBrigthness);
  MyMenu.addMenuItem(3,"RETURN","<","OK",">",0,dummy);

  MyMenu.show();
*/
}

void loop() {
/*
  M5.update();
  if(M5.BtnC.wasPressed())MyMenu.up();
  if(M5.BtnA.wasPressed())MyMenu.down();
  if(M5.BtnB.wasPressed())MyMenu.execute();
*/
}

void app_main(void) {
	static f_ptr_t ino = {setup, loop};
	initTemplateplus(ino);
}

