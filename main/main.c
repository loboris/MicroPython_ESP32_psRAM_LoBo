/*
 * Copyleft (c) 2018 mfp20; https://github.com/mfp20
 *
 * esp-idf template with added features mainly tailored for M5Stack core hw:
 * - uPython with external SPIRAM support (loboris; https://github.com/loboris )
 * - wifi raw packet stub functions (Jeija; https://github.com/Jeija )
 * - wifi push OTA stub component (yanbe; https://github.com/yanbe )
 * - arduino-esp32 libs stub (code by Espressif)
 * - arduino M5Stack lib ( https://github.com/m5stack )
 * - arduino Simple Application Menu lib (tomsuch's M5StackSAM; https://github.com/tomsuch )
 * - example apps for M5StackSAM
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#include "Arduino.h"
#include "80211raw.h"
#include "ota_server.h"

// WIFI
#if CONFIG_ENABLE_WIFI

static EventGroupHandle_t wifi_event_group;
static const int CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD

static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    wifi_config_t sta_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .bssid_set = false
        }
    };
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}
#endif

// Arduino
/*
#include <M5Stack.h>
#include <Wire.h>
#include "EEPROM.h"
#include <M5StackSAM.h>

M5SAM MyMenu;

#define EEPROM_SIZE 64

void dummy(){
}

void setup() {
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
}

void loop() {
  M5.update();
  if(M5.BtnC.wasPressed())MyMenu.up();
  if(M5.BtnA.wasPressed())MyMenu.down();
  if(M5.BtnB.wasPressed())MyMenu.execute();
}
*/

// MAIN
#if CONFIG_ENABLE_OTA
extern TaskHandle_t otaserver_entry(void);
#endif
#if CONFIG_ENABLE_ARDUINO
extern TaskHandle_t arduino_entry(void);
#endif
#if CONFIG_ENABLE_MICROPYTHON
extern TaskHandle_t micropython_entry(void);
#endif

void app_main(void) {
	ESP_ERROR_CHECK( nvs_flash_init() );

#if CONFIG_ENABLE_WIFI
	wifi_init();
#endif

#if CONFIG_ENABLE_OTA
    TaskHandle_t otat = otaserver_entry();
#endif

#ifdef CONFIG_ENABLE_ARDUINO
    // TODO: modify arduino_entry to accept 2 functions (setup,loop) as parameters
    TaskHandle_t ardt = arduino_entry();
#endif

#if CONFIG_ENABLE_MICROPYTHON
    TaskHandle_t upyt = micropython_entry();
#endif

#if CONFIG_WIFI_AUTOCONNECT
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
	vTaskResume(otat);
#endif
}
