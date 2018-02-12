#ifndef COMPONENTS_TEMPLATEPLUS_INCLUDE_TEMPLATEPLUS_H_
#define COMPONENTS_TEMPLATEPLUS_INCLUDE_TEMPLATEPLUS_H_

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"

#include "nvs_flash.h"
#include "ota_server.h"
#include "Arduino.h"

// UTILS
static esp_err_t event_handler(void *, system_event_t *);

// WIFI
#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
static const int WIFI_CONNECTED_BIT = BIT0;
static EventGroupHandle_t wifi_event_group;
static void wifi_init(void);

extern TaskHandle_t otaserver_entry(void);
extern TaskHandle_t arduino_entry(f_ptr_t);
extern TaskHandle_t micropython_entry(void);
void initTemplateplus(f_ptr_t);

#endif /* COMPONENTS_TEMPLATEPLUS_INCLUDE_TEMPLATEPLUS_H_ */
