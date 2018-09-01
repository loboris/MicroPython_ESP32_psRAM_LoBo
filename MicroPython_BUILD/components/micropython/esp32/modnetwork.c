/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 * and Mnemote Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016, 2017 Nick Moore @mnemote
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Based on esp8266/modnetwork.c which is Copyright (c) 2015 Paul Sokolovsky
 * And the ESP IDF example code which is Public Domain / CC0
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "libs/libGSM.h"
#include "py/nlr.h"
#include "py/objlist.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "netutils.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "lwip/dns.h"
#ifdef CONFIG_MICROPY_USE_MDNS
#include "mdns.h"
#endif

#include "modnetwork.h"

#define MODNETWORK_INCLUDE_CONSTANTS (1)
//#define MPY_WIFI_USED_STORAGE	WIFI_STORAGE_FLASH
#define MPY_WIFI_USED_STORAGE	WIFI_STORAGE_RAM

static const char *MODNETTWORK_TAG = "[modnetwork]";

NORETURN void _esp_exceptions(esp_err_t e) {
   switch (e) {
      case ESP_ERR_WIFI_NOT_INIT:
        mp_raise_msg(&mp_type_OSError, "Wifi Not Initialized");
        break;
      case ESP_ERR_WIFI_NOT_STARTED:
        mp_raise_msg(&mp_type_OSError, "Wifi Not Started");
        break;
      case ESP_ERR_WIFI_CONN:
        mp_raise_msg(&mp_type_OSError, "Wifi Internal Error");
        break;
      case ESP_ERR_WIFI_SSID:
        mp_raise_msg(&mp_type_OSError, "Wifi SSID Invalid");
        break;
      case ESP_FAIL:
        mp_raise_msg(&mp_type_OSError, "Wifi Internal Failure");
        break;
      case ESP_ERR_WIFI_IF:
        mp_raise_msg(&mp_type_OSError, "Wifi Invalid Interface");
        break;
      case ESP_ERR_WIFI_MAC:
        mp_raise_msg(&mp_type_OSError, "Wifi Invalid MAC Address");
        break;
      case ESP_ERR_INVALID_ARG:
        mp_raise_msg(&mp_type_OSError, "Wifi Invalid Argument");
        break;
      case ESP_ERR_WIFI_MODE:
        mp_raise_msg(&mp_type_OSError, "Wifi Invalid Mode");
        break;
      case ESP_ERR_WIFI_PASSWORD:
        mp_raise_msg(&mp_type_OSError, "Wifi Invalid Password");
        break;
      case ESP_ERR_WIFI_NVS:
        mp_raise_msg(&mp_type_OSError, "Wifi Internal NVS Error");
        break;
      case ESP_ERR_TCPIP_ADAPTER_INVALID_PARAMS:
        mp_raise_msg(&mp_type_OSError, "TCP/IP Invalid Parameters");
        break;
      case ESP_ERR_TCPIP_ADAPTER_IF_NOT_READY:
        mp_raise_msg(&mp_type_OSError, "TCP/IP IF Not Ready");
        break;
      case ESP_ERR_TCPIP_ADAPTER_DHCPC_START_FAILED:
        mp_raise_msg(&mp_type_OSError, "TCP/IP DHCP Client Start Failed");
        break;
      case ESP_ERR_WIFI_TIMEOUT:
        mp_raise_OSError(MP_ETIMEDOUT);
        break;
      case ESP_ERR_TCPIP_ADAPTER_NO_MEM:
      case ESP_ERR_NO_MEM:
        mp_raise_OSError(MP_ENOMEM);
        break;
      default:
        nlr_raise(mp_obj_new_exception_msg_varg(
          &mp_type_RuntimeError, "Wifi Unknown Error 0x%04x", e
        ));
   }
}

static const char* const wifi_events[] = {
	"WiFi ready",
	"Finish scanning AP",
	"Station start",
	"Station stop",
	"Station connected to AP",
	"Station disconnected from AP",
	"The auth mode of AP connected by station changed",
	"Station got IP from connected AP",
	"Station lost IP and the IP is reset to 0",
	"Station wps succeeds in enrollee mode",
	"Station wps fails in enrollee mode",
	"Station wps timeout in enrollee mode",
	"Station wps pin code in enrollee mode",
	"Soft-AP start",
	"Soft-AP stop",
	"Station connected to soft-AP",
	"Station disconnected from ESP32 soft-AP",
	"Soft-AP assigned an IP to a connected station",
	"Receive probe request packet in soft-AP interface",
	"Station or ap or ethernet interface v6IP addr is preferred",
	"Ethernet start",
	"Ethernet stop",
	"Ethernet phy link up",
	"Ethernet phy link down",
	"Ethernet got IP from connected AP",
};

/*
static const char* const wifi_cyphers[] = {
	"NONE",
	"WEP40",
	"WEP104",
	"TKIP",
	"CCMP",
	"TKIP_CCMP",
	"UNKNOWN",
};
*/

static const char* const wifi_auth_modes[] = {
	"OPEN",
	"WEP",
	"WPA_PSK",
	"WPA2_PSK",
	"WPA_WPA2_PSK",
	"WPA2_ENTERPRISE",
};

static inline void esp_exceptions(esp_err_t e) {
    if (e != ESP_OK) _esp_exceptions(e);
}

#define ESP_EXCEPTIONS(x) do { esp_exceptions(x); } while (0);

// global variables
int wifi_network_state = WIFI_STATE_NOTINIT;
bool wifi_sta_isconnected = false;
bool wifi_sta_has_ipaddress = false;
bool wifi_sta_changed_ipaddress = false;
bool wifi_ap_isconnected = false;
bool wifi_ap_sta_isconnected = false;
tcpip_adapter_if_t tcpip_if[MAX_ACTIVE_INTERFACES] = {TCPIP_ADAPTER_IF_MAX};

const mp_obj_type_t wlan_if_type;
const wlan_if_obj_t wlan_sta_obj = {{&wlan_if_type}, WIFI_IF_STA, WIFI_MODE_STA};
const wlan_if_obj_t wlan_ap_obj = {{&wlan_if_type}, WIFI_IF_AP, WIFI_MODE_AP};

//static wifi_config_t wifi_ap_config = { 0 };
static wifi_config_t wifi_sta_config = { 0 };

// Set to "true" if the STA interface is requested to be automatically reconnected.
static bool wifi_sta_reconnect = false;
static bool sta_isStarted = false;

static mp_obj_t event_callback = NULL;
static mp_obj_t probereq_callback = NULL;
static QueueHandle_t wifi_mutex = NULL;

static QueueHandle_t probereq_mutex = NULL;

//------------------------------------------------------------------------
static void processPROBEREQRECVED(const uint8_t *frame, int len, int rssi)
{
	if (probereq_callback != NULL) {
		if (probereq_mutex) xSemaphoreTake(probereq_mutex, 1000);

		mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_DICT);
		if (!carg) goto end;
		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_INT, rssi, NULL, "rssi")) goto end;
		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_INT, len, NULL, "len")) goto end;
		if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, len, frame, "frame")) goto end;

		mp_sched_schedule(probereq_callback, mp_const_none, carg);
end:
		if (probereq_mutex) xSemaphoreGive(probereq_mutex);
	}
}

//------------------------------------------------------
static void processEvent_callback(system_event_t *event)
{
	if (event->event_id >= SYSTEM_EVENT_MAX) return;

	mp_sched_carg_t *carg = NULL;
	if (event_callback) {
		carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
		if (carg == NULL) return;
		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_INT, event->event_id, NULL, NULL)) return;
		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen(wifi_events[event->event_id]), (const uint8_t *)wifi_events[event->event_id], NULL)) return;
	}

	switch (event->event_id) {
		case SYSTEM_EVENT_STA_CONNECTED: {
				system_event_sta_connected_t *info = (system_event_sta_connected_t *)&event->event_info;
				wifi_sta_isconnected = true;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_STR, info->ssid_len, info->ssid, "ssid")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_INT, info->channel, NULL, "channel")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		case SYSTEM_EVENT_STA_DISCONNECTED: {
				system_event_sta_disconnected_t *info = (system_event_sta_disconnected_t *)&event->event_info;
				wifi_sta_isconnected = false;
				wifi_sta_has_ipaddress = false;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_STR, info->ssid_len, info->ssid, "ssid")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_INT, info->reason, NULL, "reason")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		case SYSTEM_EVENT_AP_START: {
			wifi_ap_isconnected = true;
			break;
		}
		case SYSTEM_EVENT_AP_STOP: {
			wifi_ap_isconnected = false;
			wifi_ap_sta_isconnected = false;
			break;
		}
		case SYSTEM_EVENT_AP_STACONNECTED: {
				wifi_ap_sta_isconnected = true;
				system_event_ap_staconnected_t *info = (system_event_ap_staconnected_t *)&event->event_info;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_BYTES, 6, info->mac, "mac")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_INT, info->aid, NULL, "aid")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		case SYSTEM_EVENT_AP_STADISCONNECTED: {
				wifi_ap_sta_isconnected = false;
				system_event_ap_stadisconnected_t *info = (system_event_ap_stadisconnected_t *)&event->event_info;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_BYTES, 6, info->mac, "mac")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_INT, info->aid, NULL, "aid")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		case SYSTEM_EVENT_AP_PROBEREQRECVED: {
				system_event_ap_probe_req_rx_t *info = (system_event_ap_probe_req_rx_t *)&event->event_info;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_INT, info->rssi, NULL, "rssi")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_BYTES, 6, info->mac, "mac")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		case SYSTEM_EVENT_STA_GOT_IP: {
				system_event_sta_got_ip_t *info = (system_event_sta_got_ip_t *)&event->event_info;
				wifi_sta_has_ipaddress = true;
				wifi_sta_changed_ipaddress = info->ip_changed;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					char ip_str[16];
					mp_uint_t ip_len;
					uint8_t *ip = (uint8_t*)&info->ip_info.ip;
					ip_len = snprintf(ip_str, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_STR, ip_len, (const uint8_t *)ip_str, "ip")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_BOOL, info->ip_changed, NULL, "changed")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		case SYSTEM_EVENT_STA_LOST_IP: {
				wifi_sta_has_ipaddress = false;
				wifi_sta_changed_ipaddress = true;
				break;
			}
		case SYSTEM_EVENT_SCAN_DONE: {
				system_event_sta_scan_done_t *info = (system_event_sta_scan_done_t *)&event->event_info;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_INT, info->status, NULL, "status")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_INT, info->number, NULL, "number")) break;
					if (!make_carg_entry(darg, 2, MP_SCHED_ENTRY_TYPE_INT, info->scan_id, NULL, "scan_id")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		case SYSTEM_EVENT_STA_AUTHMODE_CHANGE: {
				system_event_sta_authmode_change_t *info = (system_event_sta_authmode_change_t *)&event->event_info;
				if (event_callback) {
					mp_sched_carg_t *darg = make_cargs(MP_SCHED_CTYPE_DICT);
					if (!carg) break;
					if (!make_carg_entry(darg, 0, MP_SCHED_ENTRY_TYPE_INT, info->old_mode, NULL, "old_mode")) break;
					if (!make_carg_entry(darg, 1, MP_SCHED_ENTRY_TYPE_INT, info->new_mode, NULL, "new_mode")) break;
					if (!make_carg_entry_carg(carg, 2, darg)) break;
				}
				break;
			}
		default:
			break;
	}
	if (carg) {
		if (carg->n < 3) {
			// the 3rd tuple item was not added, add it now
			if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_NONE, 0, NULL, NULL)) return;
		}
		mp_sched_schedule(event_callback, mp_const_none, carg);
	}
}

//------------------------
static void tryReconnect()
{
	if (wifi_sta_reconnect) {
		wifi_mode_t mode;
		if (esp_wifi_get_mode(&mode) == ESP_OK) {
			if (mode & WIFI_MODE_STA) {
				if (sta_isStarted) {
					// STA is active and started, attempt to reconnect.
					esp_err_t res = esp_wifi_connect();
					if (res != ESP_OK) {
						ESP_LOGD(MODNETTWORK_TAG, "error attempting to reconnect: (%d)", res-ESP_ERR_WIFI_BASE);
					}
				}
			}
		}
	}
}
// This function is called by the system-event task and so runs in a different
// thread to the main MicroPython task.  It must not raise any Python exceptions.
//--------------------------------------------------------------
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	if (wifi_mutex) xSemaphoreTake(wifi_mutex, 1000);

	if (wifi_network_state == WIFI_STATE_STARTED) {
		switch(event->event_id) {
		case SYSTEM_EVENT_STA_START:
			sta_isStarted = true;
			tryReconnect();
			break;
		case SYSTEM_EVENT_STA_STOP:
			sta_isStarted = false;
			break;
		case SYSTEM_EVENT_STA_DISCONNECTED: {
			// This is a workaround as ESP32 WiFi library doesn't currently auto-reconnect.
			system_event_sta_disconnected_t *disconn = &event->event_info.disconnected;
			switch (disconn->reason) {
				case WIFI_REASON_AUTH_FAIL:
					wifi_sta_reconnect = false;
					break;
				case WIFI_REASON_ASSOC_LEAVE:
					sta_isStarted = false;
					break;
				default:
					// Let other errors through and try to reconnect.
					break;
			}
			tryReconnect();
			break;
		}
		default:
			break;
		}
	}

	#ifdef CONFIG_MICROPY_USE_MDNS
	mdns_handle_system_event(ctx, event);
	#endif

	// === Handle events callbacks ===
	if (wifi_network_state == WIFI_STATE_STARTED) processEvent_callback(event);

	if (wifi_mutex) xSemaphoreGive(wifi_mutex);
	return ESP_OK;
}

//---------------------------------------------------
STATIC void require_if(mp_obj_t wlan_if, int if_no) {
    wlan_if_obj_t *self = MP_OBJ_TO_PTR(wlan_if);
    if (self->if_id != if_no) {
        mp_raise_msg(&mp_type_OSError, if_no == WIFI_IF_STA ? "STA required" : "AP required");
    }
}

//--------------------------------
STATIC mp_obj_t esp_initialize() {
    if (wifi_network_state < WIFI_STATE_INIT) {
    	// This is executed only once
        ESP_LOGD(MODNETTWORK_TAG, "Initializing TCP/IP");
        tcpip_adapter_init();
        ESP_LOGD(MODNETTWORK_TAG, "Initializing Event Loop");
        ESP_EXCEPTIONS( esp_event_loop_init(event_handler, NULL) );
        ESP_LOGD(MODNETTWORK_TAG, "Event loop initialized");

        // create mutex's
        if (wifi_mutex == NULL) wifi_mutex = xSemaphoreCreateMutex();
        if (probereq_mutex == NULL) probereq_mutex = xSemaphoreCreateMutex();
        // add probe requests handler
        esp_wifi_set_sta_rx_probe_req(processPROBEREQRECVED);

        wifi_network_state = WIFI_STATE_INIT;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(esp_initialize_obj, esp_initialize);

#if (WIFI_MODE_STA & WIFI_MODE_AP != WIFI_MODE_NULL || WIFI_MODE_STA | WIFI_MODE_AP != WIFI_MODE_APSTA)
#error WIFI_MODE_STA and WIFI_MODE_AP are supposed to be bitfields!
#endif


// Return WLAN object for given WiFi mode (default: STA)
// Does not start WiFi if not started !
//-------------------------------------------------------------
STATIC mp_obj_t get_wlan(size_t n_args, const mp_obj_t *args) {
    if (wifi_network_state < WIFI_STATE_INIT) {
        mp_raise_ValueError("TCT/IP Adapter not initialized");
    }

    // Default mode
    int if_id = WIFI_IF_STA;

	if (n_args > 0) {
		// Get required WiFi mode
		if_id = mp_obj_get_int(args[0]);
		if ((if_id != WIFI_IF_STA) && (if_id != WIFI_IF_AP)) {
			mp_raise_ValueError("invalid WLAN interface identifier");
		}
	}

	// Return the WLAN object
    if (if_id == WIFI_IF_STA) return MP_OBJ_FROM_PTR(&wlan_sta_obj);
    return MP_OBJ_FROM_PTR(&wlan_ap_obj);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(get_wlan_obj, 0, 1, get_wlan);


//----------------------
static void _init_wifi()
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(MODNETTWORK_TAG, "Error initializing WiFi (%d)", ret);
		mp_raise_OSError(ret);
	}
	ret = esp_wifi_set_storage(MPY_WIFI_USED_STORAGE);
	if (ret != ESP_OK) {
		ESP_LOGE(MODNETTWORK_TAG, "Error initializing WiFi storage (%d)", ret);
		mp_raise_OSError(ret);
	}
	ESP_LOGD(MODNETTWORK_TAG, "WiFi Initialized");
    wifi_network_state = WIFI_STATE_STOPPED;
}

// Initialize WiFi if needed, set the requested mode and start WiFi
//------------------------------------------------------
static void _wifi_init(wifi_mode_t mode, bool reconnect)
{
	if (wifi_network_state < WIFI_STATE_STOPPED) _init_wifi();

    esp_err_t ret = 0;
	if (wifi_network_state == WIFI_STATE_STARTED) {
		// Stop WiFi
		wifi_network_state = WIFI_STATE_STOPPED;
		wifi_sta_isconnected = false;
		wifi_sta_has_ipaddress = false;
		wifi_sta_changed_ipaddress = false;
		wifi_ap_isconnected = false;
		wifi_ap_sta_isconnected = false;
		ret = esp_wifi_stop();
		if (ret != ESP_OK) {
			ESP_LOGE(MODNETTWORK_TAG, "Error stopping WiFi (%d)", ret);
			goto exit_error;
		}
		ESP_LOGD(MODNETTWORK_TAG, "WiFi Stopped");
	}

    // Set WiFi mode
	esp_wifi_set_mode(mode);
    if (ret != ESP_OK) {
        ESP_LOGE(MODNETTWORK_TAG, "Error setting WiFi mode (%d)", ret);
        goto exit_error;
    }
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(MODNETTWORK_TAG, "Error starting WiFi (%d)", ret);
        goto exit_error;
    }
    wifi_sta_reconnect = reconnect;

    wifi_network_state = WIFI_STATE_STARTED;
    ESP_LOGD(MODNETTWORK_TAG, "WiFi Started, mode %d", mode);
    return;

exit_error:
	wifi_network_state = WIFI_STATE_INIT;
	ret = esp_wifi_stop();
	if (ret != ESP_OK) {
		ESP_LOGE(MODNETTWORK_TAG, "Error stopping WiFi (%d)", ret);
	}
	vTaskDelay(5 / portTICK_PERIOD_MS);
	esp_wifi_deinit();
	if (ret != ESP_OK) {
		ESP_LOGE(MODNETTWORK_TAG, "Error deinitializing WiFi (%d)", ret);
	}
	vTaskDelay(5 / portTICK_PERIOD_MS);
	mp_raise_OSError(ret);
}

// Activate (start) or deactivate (stop) Wifi
//-------------------------------------------------------------
STATIC mp_obj_t esp_active(size_t n_args, const mp_obj_t *args)
{
	if (wifi_network_state < WIFI_STATE_INIT) {
        ESP_LOGW(MODNETTWORK_TAG, "WiFi not initialized");
		return mp_const_false;
	}

	wlan_if_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	wifi_mode_t mode;

    if (n_args < 2) goto exit;

	// Get requested action
	bool active = mp_obj_is_true(args[1]);

	if (active) {
		// === WiFi activation requested ===
		if (wifi_network_state == WIFI_STATE_STARTED) {
			// WiFi already started, check mode
			esp_err_t ret = esp_wifi_get_mode(&mode);
			if (ret != ESP_OK) {
				ESP_LOGE(MODNETTWORK_TAG, "Error getting WiFi mode (%d)", ret);
				return mp_const_false;
			}
			// If requested mode is already started, just return
			if (self->wifi_mode & mode) return mp_const_true;
			// Restart WiFi adding a new mode
	        _wifi_init(mode | self->wifi_mode, (mode & WIFI_MODE_STA) & wifi_sta_reconnect);
		}
		else {
			// Start WiFi
	        _wifi_init(self->wifi_mode, false);
		}
	}
	else {
		// === WiFi deactivation requested ===
		if (wifi_network_state == WIFI_STATE_STARTED) {
			// Get current mode
			esp_err_t ret = esp_wifi_get_mode(&mode);
			if (ret != ESP_OK) {
				ESP_LOGE(MODNETTWORK_TAG, "Error getting WiFi mode (%d)", ret);
				return mp_const_false;
			}
			if (self->wifi_mode & mode) {
				wifi_mode_t new_mode = mode & ~self->wifi_mode;
				if (new_mode == 0) {
					// No mode is active, stop and deinitialize WiFi
					wifi_network_state = WIFI_STATE_INIT;
					wifi_sta_isconnected = false;
					wifi_sta_has_ipaddress = false;
					wifi_sta_changed_ipaddress = false;
					wifi_ap_isconnected = false;
					wifi_ap_sta_isconnected = false;
					ret = esp_wifi_stop();
					if (ret != ESP_OK) {
						ESP_LOGE(MODNETTWORK_TAG, "Error stopping WiFi (%d)", ret);
					}
					vTaskDelay(5 / portTICK_PERIOD_MS);
					esp_wifi_deinit();
					if (ret != ESP_OK) {
						ESP_LOGE(MODNETTWORK_TAG, "Error deinitializing WiFi (%d)", ret);
					}
					vTaskDelay(5 / portTICK_PERIOD_MS);
				    ESP_LOGI(MODNETTWORK_TAG, "WiFi Stopped");
				}
				else {
			        _wifi_init(new_mode, (new_mode & WIFI_MODE_STA) & wifi_sta_reconnect);
				}
			}
		}
	}

exit:
	// === Return wifi status (started/not started) ===
	if (wifi_network_state != WIFI_STATE_STARTED) return mp_const_false;
	// Get current mode
	esp_err_t ret = esp_wifi_get_mode(&mode);
	if (ret != ESP_OK) {
		ESP_LOGE(MODNETTWORK_TAG, "Error getting WiFi mode (%d)", ret);
		return mp_const_false;
	}
	if (self->wifi_mode & mode) return mp_const_true;
	return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_active_obj, 1, 2, esp_active);

//------------------------------------------
static bool _check_wifi_started(bool except)
{
	if (wifi_network_state < WIFI_STATE_STARTED) {
		if (except) {
	        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "WiFi not started"));
		}
		else {
			ESP_LOGW(MODNETTWORK_TAG, "WiFi not started");
			return false;
		}
	}
	return true;
}

// Connect to access point (only in STA mode)
//----------------------------------------------------------------
STATIC mp_obj_t esp_connect(size_t n_args, const mp_obj_t *args) {

	if (!_check_wifi_started(false)) return mp_const_none;

	wlan_if_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	if (self->wifi_mode != WIFI_MODE_STA) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Not supported in AP mode"));
	}

	wifi_mode_t mode;
	esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret != ESP_OK) {
        ESP_LOGE(MODNETTWORK_TAG, "Error getting WiFi mode (%d)", ret);
        return mp_const_none;
    }
    // Only connect if in STA mode
    if ((mode & WIFI_MODE_STA) == 0) {
        ESP_LOGE(MODNETTWORK_TAG, "STA mode not started");
    	return mp_const_none;
    }

    mp_uint_t len;
    const char *p;
    if (n_args > 1) {
    	// Get SSID
        memset(&wifi_sta_config, 0, sizeof(wifi_sta_config));
        p = mp_obj_str_get_data(args[1], &len);
        memcpy(wifi_sta_config.sta.ssid, p, MIN(len, sizeof(wifi_sta_config.sta.ssid)));
        // Get password (optional)
        p = (n_args > 2) ? mp_obj_str_get_data(args[2], &len) : "";
        memcpy(wifi_sta_config.sta.password, p, MIN(len, sizeof(wifi_sta_config.sta.password)));
        if ((n_args > 3)) {
        	// Get channel (optional
        	int chan = mp_obj_get_int(args[3]);
        	if ((chan >= 1) && (chan <= 13)) wifi_sta_config.sta.channel = chan;
        }
        ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_sta_config);
        if (ret != ESP_OK) {
            ESP_LOGE(MODNETTWORK_TAG, "Error configuring WiFi (%d)", ret);
            return mp_const_none;
        }
    }

    MP_THREAD_GIL_EXIT();
    ret = esp_wifi_connect();
    MP_THREAD_GIL_ENTER();
    if (ret == ESP_OK) wifi_sta_reconnect = true;
    else {
        ESP_LOGE(MODNETTWORK_TAG, "Error connecting to AP (%d)", ret);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_connect_obj, 1, 7, esp_connect);

//------------------------------------------------
STATIC mp_obj_t esp_disconnect(mp_obj_t self_in) {
	if (!_check_wifi_started(false)) return mp_const_none;

	wlan_if_obj_t *self = MP_OBJ_TO_PTR(self_in);
	if (self->wifi_mode != WIFI_MODE_STA) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Not supported in AP mode"));
	}

	if (wifi_sta_reconnect) {
    	esp_err_t ret = esp_wifi_disconnect();
        if (ret != ESP_OK) {
            ESP_LOGW(MODNETTWORK_TAG, "Error disconnecting from AP (%d)", ret);
        }
        else wifi_sta_reconnect = false;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp_disconnect_obj, esp_disconnect);

//---------------------------------------------------------------
STATIC mp_obj_t esp_status(size_t n_args, const mp_obj_t *args) {
	if (!_check_wifi_started(false)) return mp_const_none;

	if (n_args == 1) {
        // no arguments: return None until link status is implemented
        return mp_const_none;
    }

    // one argument: return status based on query parameter
    switch ((uintptr_t)args[1]) {
        case (uintptr_t)MP_OBJ_NEW_QSTR(MP_QSTR_stations): {
            // return list of connected stations, only if in soft-AP mode
            require_if(args[0], WIFI_IF_AP);
            wifi_sta_list_t station_list;
            ESP_EXCEPTIONS(esp_wifi_ap_get_sta_list(&station_list));
            wifi_sta_info_t *stations = (wifi_sta_info_t*)station_list.sta;
            mp_obj_t list = mp_obj_new_list(0, NULL);
            for (int i = 0; i < station_list.num; ++i) {
            	ip4_addr_t addr;
                mp_obj_tuple_t *t = mp_obj_new_tuple(3, NULL);
                t->items[0] = mp_obj_new_bytes(stations[i].mac, sizeof(stations[i].mac));
                if (dhcp_search_ip_on_mac(stations[i].mac , &addr)) {
                	t->items[1] = netutils_format_ipv4_addr((uint8_t*)&addr.addr, NETUTILS_BIG);
                }
                else t->items[1] = mp_const_none;
                t->items[2] = MP_OBJ_NEW_SMALL_INT(stations[i].rssi);
                mp_obj_list_append(list, t);
            }
            return list;
        }

        default:
            mp_raise_ValueError("unknown status param");
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_status_obj, 1, 2, esp_status);

//-------------------------------------------------------------
STATIC mp_obj_t esp_scan(size_t n_args, const mp_obj_t *args) {
	_check_wifi_started(true);

	// check that STA mode is active
    wifi_mode_t mode;
	esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error getting WiFi mode"));
    }
    if ((mode & WIFI_MODE_STA) == 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "STA must be active"));
    }

    mp_obj_t list = mp_obj_new_list(0, NULL);
    wifi_scan_config_t config = { 0 };
    if (n_args > 1) config.show_hidden = mp_obj_is_true(args[1]);

    MP_THREAD_GIL_EXIT();
    esp_err_t status = esp_wifi_scan_start(&config, 1);
    MP_THREAD_GIL_ENTER();

    if (status == 0) {
        uint16_t count = 0;
        ESP_EXCEPTIONS( esp_wifi_scan_get_ap_num(&count) );
        wifi_ap_record_t *wifi_ap_records = calloc(count, sizeof(wifi_ap_record_t));
        ESP_EXCEPTIONS( esp_wifi_scan_get_ap_records(&count, wifi_ap_records) );
        for (uint16_t i = 0; i < count; i++) {
            mp_obj_tuple_t *t = mp_obj_new_tuple(7, NULL);
            uint8_t *x = memchr(wifi_ap_records[i].ssid, 0, sizeof(wifi_ap_records[i].ssid));
            int ssid_len = x ? x - wifi_ap_records[i].ssid : sizeof(wifi_ap_records[i].ssid);
            t->items[0] = mp_obj_new_bytes(wifi_ap_records[i].ssid, ssid_len);
            t->items[1] = mp_obj_new_bytes(wifi_ap_records[i].bssid, sizeof(wifi_ap_records[i].bssid));
            t->items[2] = MP_OBJ_NEW_SMALL_INT(wifi_ap_records[i].primary);
            t->items[3] = MP_OBJ_NEW_SMALL_INT(wifi_ap_records[i].rssi);
            t->items[4] = MP_OBJ_NEW_SMALL_INT(wifi_ap_records[i].authmode);
            t->items[5] = mp_obj_new_str(wifi_auth_modes[wifi_ap_records[i].authmode], strlen(wifi_auth_modes[wifi_ap_records[i].authmode]));
            t->items[6] = (ssid_len == 0) ? mp_const_true : mp_const_false;
            mp_obj_list_append(list, MP_OBJ_FROM_PTR(t));
        }
        free(wifi_ap_records);
    }
    return list;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_scan_obj, 1, 2, esp_scan);

//--------------------------------------------------------------------
STATIC mp_obj_t esp_isconnected(size_t n_args, const mp_obj_t *args) {
	if (!_check_wifi_started(false)) return mp_obj_new_bool(false);

	wlan_if_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	bool check_clients = true;
    if (n_args > 1) check_clients = mp_obj_is_true(args[1]);

    if (self->if_id == WIFI_IF_STA) {
        return mp_obj_new_bool(((wifi_sta_isconnected) && (wifi_sta_has_ipaddress)));
    }
    else {
    	bool res = wifi_ap_isconnected;
    	if ((res) && (check_clients)) {
			wifi_sta_list_t sta;
			esp_wifi_ap_get_sta_list(&sta);
			res = (sta.num != 0);
    	}
    	return mp_obj_new_bool(res);
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_isconnected_obj, 1, 2, esp_isconnected);

//---------------------------
static bool wifi_is_started()
{
	wifi_mode_t wifi_mode;
    esp_err_t ret = esp_wifi_get_mode(&wifi_mode);
    if (ret != ESP_OK) return false;

    bool sta_f = ((wifi_sta_isconnected) && (wifi_sta_has_ipaddress));
    bool ap_f = wifi_ap_isconnected;
    if (wifi_mode == WIFI_MODE_STA) return sta_f;
    else if (wifi_mode == WIFI_MODE_AP) return ap_f;
    else if (wifi_mode == WIFI_MODE_APSTA) return (sta_f | ap_f);
	return false;
}

//----------------------------------------------
STATIC mp_obj_t esp_isactive(mp_obj_t self_in) {
	if (wifi_network_state < WIFI_STATE_STARTED) return mp_obj_new_bool(false);

	return mp_obj_new_bool(wifi_is_started());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp_isactive_obj, esp_isactive);

//-----------------------------------------------------------------
STATIC mp_obj_t esp_ifconfig(size_t n_args, const mp_obj_t *args) {
    if (wifi_network_state < WIFI_STATE_INIT) {
        mp_raise_ValueError("TCT/IP Adapter not initialized");
    }

	wlan_if_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    tcpip_adapter_ip_info_t info;
    tcpip_adapter_dns_info_t dns_info;

    tcpip_adapter_get_ip_info(self->if_id, &info);
    tcpip_adapter_get_dns_info(self->if_id, TCPIP_ADAPTER_DNS_MAIN, &dns_info);
    if (n_args == 1) {
        // === Get configuration ===
        mp_obj_t tuple[4] = {
            netutils_format_ipv4_addr((uint8_t*)&info.ip, NETUTILS_BIG),
            netutils_format_ipv4_addr((uint8_t*)&info.netmask, NETUTILS_BIG),
            netutils_format_ipv4_addr((uint8_t*)&info.gw, NETUTILS_BIG),
            netutils_format_ipv4_addr((uint8_t*)&dns_info.ip, NETUTILS_BIG),
        };
        // Return tuple: (ip, netmask, gateway, dns_ip)
        return mp_obj_new_tuple(4, tuple);
    }
    else {
        // === set configuration parameters from tuple: (ip, netmask, gateway, dns_ip) ===
        mp_obj_t *items;
        mp_obj_get_array_fixed_n(args[1], 4, &items);

        // Static IP
        netutils_parse_ipv4_addr(items[0], (void*)&info.ip, NETUTILS_BIG);
        if (mp_obj_is_integer(items[1])) {
            // allow numeric netmask, i.e.:
            // 24 -> 255.255.255.0
            // 16 -> 255.255.0.0
            // etc...
            uint32_t* m = (uint32_t*)&info.netmask;
            *m = htonl(0xffffffff << (32 - mp_obj_get_int(items[1])));
        }
        else {
            netutils_parse_ipv4_addr(items[1], (void*)&info.netmask, NETUTILS_BIG);
        }
        // net mask
        netutils_parse_ipv4_addr(items[2], (void*)&info.gw, NETUTILS_BIG);
        // gateway
        netutils_parse_ipv4_addr(items[3], (void*)&dns_info.ip, NETUTILS_BIG);

        // To set a static IP we have to disable DHCP first
        if ((self->if_id == WIFI_IF_STA) || (self->if_id == ESP_IF_ETH)) {
            esp_err_t e = tcpip_adapter_dhcpc_stop(self->if_id);
            if (e != ESP_OK && e != ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STOPPED) _esp_exceptions(e);
            ESP_EXCEPTIONS(tcpip_adapter_set_ip_info(self->if_id, &info));
            ESP_EXCEPTIONS(tcpip_adapter_set_dns_info(self->if_id, TCPIP_ADAPTER_DNS_MAIN, &dns_info));
        }
        else if (self->if_id == WIFI_IF_AP) {
            esp_err_t e = tcpip_adapter_dhcps_stop(WIFI_IF_AP);
            if (e != ESP_OK && e != ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STOPPED) _esp_exceptions(e);
            ESP_EXCEPTIONS(tcpip_adapter_set_ip_info(WIFI_IF_AP, &info));
            ESP_EXCEPTIONS(tcpip_adapter_set_dns_info(WIFI_IF_AP, TCPIP_ADAPTER_DNS_MAIN, &dns_info));
            ESP_EXCEPTIONS(tcpip_adapter_dhcps_start(WIFI_IF_AP));
        }
        return mp_const_none;
    }
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_ifconfig_obj, 1, 2, esp_ifconfig);

//--------------------------------------------------------------------------------------
static mp_obj_t get_config_param(uintptr_t arg, wlan_if_obj_t *self, wifi_config_t *cfg)
{
    mp_obj_t val = mp_const_none;

    #define QS(x) (uintptr_t)MP_OBJ_NEW_QSTR(x)
    mp_obj_tuple_t *t;
    switch (arg) {
        case QS(MP_QSTR_mac): {
            uint8_t mac[6];
            if (esp_wifi_get_mac(self->if_id, mac) != ESP_OK) val = mp_const_false;
            else val = mp_obj_new_bytes(mac, sizeof(mac));
            break;
        }
        case QS(MP_QSTR_essid):
            if (self->if_id == WIFI_IF_AP) val = mp_obj_new_str((char*)cfg->ap.ssid, cfg->ap.ssid_len);
            break;
        case QS(MP_QSTR_hidden):
			if (self->if_id == WIFI_IF_AP) val = mp_obj_new_bool(cfg->ap.ssid_hidden);
            break;
        case QS(MP_QSTR_authmode):
			if (self->if_id == WIFI_IF_AP) {
				t = mp_obj_new_tuple(2, NULL);
				t->items[0] = MP_OBJ_NEW_SMALL_INT(cfg->ap.authmode);
				t->items[1] = mp_obj_new_str(wifi_auth_modes[cfg->ap.authmode], strlen(wifi_auth_modes[cfg->ap.authmode]));
				val = MP_OBJ_FROM_PTR(t);
			}
            break;
        case QS(MP_QSTR_mode):
        	t = mp_obj_new_tuple(2, NULL);
        	t->items[0] = MP_OBJ_NEW_SMALL_INT(self->if_id);
			if (self->if_id == WIFI_IF_STA)
				t->items[1] = mp_obj_new_str("STA_IF", 6);
			else t->items[1] = mp_obj_new_str("AP_IF", 5);
            val = MP_OBJ_FROM_PTR(t);
            break;
        case QS(MP_QSTR_wifimode):
        	t = mp_obj_new_tuple(2, NULL);
        	wifi_mode_t mode;
			esp_err_t ret = esp_wifi_get_mode(&mode);
			if (ret == ESP_OK) {
	        	t->items[0] = MP_OBJ_NEW_SMALL_INT(mode);
				if (mode ==WIFI_MODE_STA) t->items[1] = mp_obj_new_str("STA", 3);
				else if (mode ==WIFI_MODE_AP) t->items[1] = mp_obj_new_str("AP", 2);
				else if (mode ==WIFI_MODE_APSTA) t->items[1] = mp_obj_new_str("APSTA", 5);
				else t->items[1] = mp_obj_new_str("Unknown", 7);
			}
			else {
	        	t->items[0] = MP_OBJ_NEW_SMALL_INT(0);
				t->items[1] = mp_obj_new_str("Unknown", 7);
			}
            val = MP_OBJ_FROM_PTR(t);
            break;
        case QS(MP_QSTR_channel):
			if (self->if_id == WIFI_IF_AP) val = MP_OBJ_NEW_SMALL_INT(cfg->ap.channel);
            break;
        case QS(MP_QSTR_dhcp_hostname): {
            const char *s;
            if (tcpip_adapter_get_hostname(self->if_id, &s) != ESP_OK) val = mp_const_false;
            else val = mp_obj_new_str(s, strlen(s));
            break;
        }
        default:
        	val = mp_obj_new_str("Unknown config param", 20);
    }
    #undef QS

    return val;
}

// Set or get wifi configuration parameters
//---------------------------------------------------------------------------------
STATIC mp_obj_t esp_config(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
	//_check_wifi_started(true);
	if (wifi_network_state < WIFI_STATE_STOPPED) _init_wifi();

	if (n_args != 1 && kwargs->used != 0) {
        mp_raise_TypeError("either pos or kw args are allowed");
    }

    wlan_if_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    // get the config for the interface
    wifi_config_t cfg;
    ESP_EXCEPTIONS(esp_wifi_get_config(self->if_id, &cfg));

    if (kwargs->used != 0) {

        for (size_t i = 0; i < kwargs->alloc; i++) {
            if (MP_MAP_SLOT_IS_FILLED(kwargs, i)) {
                int req_if = -1;

                #define QS(x) (uintptr_t)MP_OBJ_NEW_QSTR(x)
                switch ((uintptr_t)kwargs->table[i].key) {
                    case QS(MP_QSTR_mac): {
                        mp_buffer_info_t bufinfo;
                        mp_get_buffer_raise(kwargs->table[i].value, &bufinfo, MP_BUFFER_READ);
                        if (bufinfo.len != 6) {
                            mp_raise_ValueError("invalid buffer length");
                        }
                        ESP_EXCEPTIONS(esp_wifi_set_mac(self->if_id, bufinfo.buf));
                        break;
                    }
                    case QS(MP_QSTR_essid): {
                        req_if = WIFI_IF_AP;
                        mp_uint_t len;
                        const char *s = mp_obj_str_get_data(kwargs->table[i].value, &len);
                        len = MIN(len, sizeof(cfg.ap.ssid));
                        memcpy(cfg.ap.ssid, s, len);
                        cfg.ap.ssid_len = len;
                        break;
                    }
                    case QS(MP_QSTR_hidden): {
                        req_if = WIFI_IF_AP;
                        cfg.ap.ssid_hidden = mp_obj_is_true(kwargs->table[i].value);
                        break;
                    }
                    case QS(MP_QSTR_authmode): {
                        req_if = WIFI_IF_AP;
                        cfg.ap.authmode = mp_obj_get_int(kwargs->table[i].value);
                        break;
                    }
                    case QS(MP_QSTR_password): {
                        req_if = WIFI_IF_AP;
                        mp_uint_t len;
                        const char *s = mp_obj_str_get_data(kwargs->table[i].value, &len);
                        len = MIN(len, sizeof(cfg.ap.password) - 1);
                        memcpy(cfg.ap.password, s, len);
                        cfg.ap.password[len] = 0;
                        break;
                    }
                    case QS(MP_QSTR_channel): {
                        req_if = WIFI_IF_AP;
                        cfg.ap.channel = mp_obj_get_int(kwargs->table[i].value);
                        break;
                    }
                    case QS(MP_QSTR_dhcp_hostname): {
                        const char *s = mp_obj_str_get_str(kwargs->table[i].value);
                        ESP_EXCEPTIONS(tcpip_adapter_set_hostname(self->if_id, s));
                        break;
                    }
                    default:
                        mp_raise_ValueError("unknown config param");
                }
                #undef QS

                // We post-check interface requirements to save on code size
                if (req_if >= 0) {
                    require_if(args[0], req_if);
                }
            }
        }

        ESP_EXCEPTIONS(esp_wifi_set_config(self->if_id, &cfg));

        return mp_const_none;
    }

    // Get config
    if (n_args != 2) {
        mp_raise_TypeError("only one query argument allowed");
    }

    mp_obj_t val;
	#define QS(x) (uintptr_t)MP_OBJ_NEW_QSTR(x)
    bool get_all = ((uintptr_t)args[1] == QS(MP_QSTR_all));

    if (get_all) {
    	// Get all config parameters
    	mp_obj_dict_t *dct = mp_obj_new_dict(0);

    	val = get_config_param(QS(MP_QSTR_mac), self, &cfg);
    	if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("mac", 3), val);
		val = get_config_param(QS(MP_QSTR_essid), self, &cfg);
		if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("essid", 5), val);
		val = get_config_param(QS(MP_QSTR_hidden), self, &cfg);
		if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("hidden", 6), val);
		val = get_config_param(QS(MP_QSTR_authmode), self, &cfg);
		if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("authmode", 8), val);
		val = get_config_param(QS(MP_QSTR_mode), self, &cfg);
		if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("mode", 4), val);
		val = get_config_param(QS(MP_QSTR_wifimode), self, &cfg);
		if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("wifimode", 8), val);
		val = get_config_param(QS(MP_QSTR_channel), self, &cfg);
		if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("channel", 7), val);
		val = get_config_param(QS(MP_QSTR_dhcp_hostname), self, &cfg);
		if ((val != mp_const_none) && (val != mp_const_false)) mp_obj_dict_store(dct, mp_obj_new_str("dhcp_hostname", 13), val);

    	val = dct;
    }
    else {
    	// Get one config parameter
		val = get_config_param((uintptr_t)args[1], self, &cfg);
		if (val == mp_const_none) {
			mp_raise_msg(&mp_type_OSError, self->if_id == WIFI_IF_STA ? "AP required" : "STA required");
		}
		if (val == mp_const_false) {
			mp_raise_msg(&mp_type_OSError, "Parameter not available");
		}
    }
	#undef QS

    return val;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(esp_config_obj, 1, esp_config);

//---------------------------------------------------------------
STATIC mp_obj_t esp_callback(size_t n_args, const mp_obj_t *args)
{
    if (n_args == 1) {
    	if (event_callback == NULL) return mp_const_false;
    	return mp_const_true;
    }

	if (wifi_mutex) xSemaphoreTake(wifi_mutex, 1000);
    if ((MP_OBJ_IS_FUN(args[1])) || (MP_OBJ_IS_METH(args[1]))) {
		event_callback = args[1];
    }
    else event_callback = NULL;
	if (wifi_mutex) xSemaphoreGive(wifi_mutex);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_callback_obj, 1, 2, esp_callback);

//------------------------------------------------------------------------
STATIC mp_obj_t esp_probereq_callback(size_t n_args, const mp_obj_t *args)
{
    if (n_args == 1) {
    	if (probereq_callback == NULL) return mp_const_false;
    	return mp_const_true;
    }

	if (probereq_mutex) xSemaphoreTake(probereq_mutex, 1000);
    if ((MP_OBJ_IS_FUN(args[1])) || (MP_OBJ_IS_METH(args[1]))) {
		probereq_callback = args[1];
    }
    else probereq_callback = NULL;
	if (probereq_mutex) xSemaphoreGive(probereq_mutex);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_probereq_callback_obj, 1, 2, esp_probereq_callback);


//========================================================
STATIC const mp_map_elem_t wlan_if_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_active),		(mp_obj_t)&esp_active_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_connect),		(mp_obj_t)&esp_connect_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_disconnect),	(mp_obj_t)&esp_disconnect_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_status),		(mp_obj_t)&esp_status_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_scan),		(mp_obj_t)&esp_scan_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_isconnected),	(mp_obj_t)&esp_isconnected_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_wifiactive),	(mp_obj_t)&esp_isactive_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_config),		(mp_obj_t)&esp_config_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_ifconfig),	(mp_obj_t)&esp_ifconfig_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_eventCB),		(mp_obj_t)&esp_callback_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_probereqCB),	(mp_obj_t)&esp_probereq_callback_obj },
};
STATIC MP_DEFINE_CONST_DICT(wlan_if_locals_dict, wlan_if_locals_dict_table);

//----------------------------------
const mp_obj_type_t wlan_if_type = {
    { &mp_type_type },
    .name = MP_QSTR_WLAN,
    .locals_dict = (mp_obj_t)&wlan_if_locals_dict,
};

STATIC mp_obj_t esp_phy_mode(size_t n_args, const mp_obj_t *args) {
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_phy_mode_obj, 0, 1, esp_phy_mode);


//---------------------------------
int network_get_active_interfaces()
{
    int n_if = 0;

    for (int i=0; i<MAX_ACTIVE_INTERFACES; i++) {
        tcpip_if[i] = TCPIP_ADAPTER_IF_MAX;
    }
    if ((wifi_network_state == WIFI_STATE_STARTED) && (wifi_is_started())) {
        wifi_mode_t mode;
        esp_err_t ret = esp_wifi_get_mode(&mode);
        if (ret == ESP_OK) {
            if (mode == WIFI_MODE_STA) {
                n_if = 1;
                tcpip_if[0] = TCPIP_ADAPTER_IF_STA;
            }
            else if (mode == WIFI_MODE_AP) {
                n_if = 1;
                tcpip_if[0] = TCPIP_ADAPTER_IF_AP;
            }
            else if (mode == WIFI_MODE_APSTA) {
                n_if = 2;
                tcpip_if[0] = TCPIP_ADAPTER_IF_STA;
                tcpip_if[1] = TCPIP_ADAPTER_IF_AP;
            }
        }
    }

    #ifdef CONFIG_MICROPY_USE_ETHERNET
    if (lan_eth_active) {
        n_if++;
        tcpip_if[n_if-1] = TCPIP_ADAPTER_IF_ETH;
    }
    #endif

    return n_if;
}

//----------------------
uint32_t network_hasip()
{
    tcpip_adapter_ip_info_t ip_info = {0};
    int n_if = network_get_active_interfaces();
    if (n_if) {
        for (int i=0; i<n_if; i++) {
            tcpip_adapter_get_ip_info(tcpip_if[i], &ip_info);
            if (ip_info.ip.addr > 0) {
                return ip_info.ip.addr;
            }
        }
    }
    return 0;
}

//--------------------------
uint32_t network_has_staip()
{
    tcpip_adapter_ip_info_t ip_info = {0};
    int n_if = network_get_active_interfaces();
    if (n_if) {
        for (int i=0; i<n_if; i++) {
            tcpip_adapter_get_ip_info(tcpip_if[i], &ip_info);
            if (((tcpip_if[i] == TCPIP_ADAPTER_IF_STA) || (tcpip_if[i] == TCPIP_ADAPTER_IF_ETH)) && (ip_info.ip.addr > 0)) {
                return ip_info.ip.addr;
            }
        }
    }
    return 0;
}

//----------------------------
void network_checkConnection()
{
    uint32_t ip = network_has_staip();
    if (ip == 0) {
        #ifdef CONFIG_MICROPY_USE_GSM
        if (ppposStatus(NULL, NULL, NULL) != GSM_STATE_CONNECTED) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "No Internet connection"));
        }
        #else
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "No Internet connection"));
        #endif
    }
}



#ifdef CONFIG_MICROPY_USE_MQTT
extern const mp_obj_type_t mqtt_type;
#endif


// ==============================
// ==== FTP & Telnet services ===

#if defined(CONFIG_MICROPY_USE_TELNET) || defined(CONFIG_MICROPY_USE_FTPSERVER)
#include "mpthreadport.h"

//-----------------------------
static mp_obj_t get_listen_ip()
{
    tcpip_adapter_ip_info_t info;
    int n_if = network_get_active_interfaces();

    if (n_if > 0) {
        mp_obj_t ip_tuple[n_if];
        for (int i=0; i<n_if; i++) {
            tcpip_adapter_get_ip_info(tcpip_if[i], &info);
            ip_tuple[i] = netutils_format_ipv4_addr((uint8_t*)&info.ip, NETUTILS_BIG);
        }
        return mp_obj_new_tuple(n_if, ip_tuple);
    }
    return mp_const_none;
}

#endif

//==============================
#ifdef CONFIG_MICROPY_USE_TELNET

#include "libs/telnet.h"

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t mod_network_startTelnet(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t allowed_args[] = {
			{ MP_QSTR_user,         MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_password,     MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_timeout,		MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = TELNET_DEF_TIMEOUT_MS/1000} },
            #if TELNET_LOGIN_MSG_LEN_MAX > 0
            { MP_QSTR_login_msg,    MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            #endif
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if ((wifi_network_state < WIFI_STATE_STARTED) || (!wifi_is_started())) {
        #ifdef CONFIG_MICROPY_USE_ETHERNET
        if (!lan_eth_active) {
            ESP_LOGE("[Telnet_start]", "Network interface not started or not connected");
            return mp_const_false;
        }
        #else
        return mp_const_false;
        #endif
    }

	if (MP_OBJ_IS_STR(args[0].u_obj)) {
        snprintf(telnet_user, TELNET_USER_PASS_LEN_MAX, mp_obj_str_get_str(args[0].u_obj));
    }
	else strcpy(telnet_user, TELNET_DEF_USER);

	if (MP_OBJ_IS_STR(args[1].u_obj)) {
    	snprintf(telnet_pass, TELNET_USER_PASS_LEN_MAX, mp_obj_str_get_str(args[1].u_obj));
    }
	else strcpy(telnet_pass, TELNET_DEF_PASS);

    #if TELNET_LOGIN_MSG_LEN_MAX > 0
    if (MP_OBJ_IS_STR(args[3].u_obj)) {
        snprintf(telnet_login_success, TELNET_LOGIN_MSG_LEN_MAX, mp_obj_str_get_str(args[3].u_obj));
    }
    else snprintf(telnet_login_success, TELNET_LOGIN_MSG_LEN_MAX, "\r\nLogin succeeded!\r\nType \"help()\" for more information.\r\n");
    #endif
    telnet_timeout = args[2].u_int * 1000;
    if ((telnet_timeout < 120000) || (telnet_timeout > 86400000)) telnet_timeout = TELNET_DEF_TIMEOUT_MS;

    if (mp_thread_createTelnetTask(TELNET_STACK_LEN)) {
        ESP_LOGI("[Telnet]","user: %s; pass: %s; timeout: %d\n", telnet_user, telnet_pass, telnet_timeout);
    	return mp_const_true;
    }
	return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mod_network_startTelnet_obj, 0, mod_network_startTelnet);

//--------------------------------------
STATIC mp_obj_t mod_network_pauseTelnet()
{
	if (telnet_disable()) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_pauseTelnet_obj, mod_network_pauseTelnet);

//---------------------------------------
STATIC mp_obj_t mod_network_resumeTelnet()
{
	if (telnet_enable()) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_resumeTelnet_obj, mod_network_resumeTelnet);

//--------------------------------------
STATIC mp_obj_t mod_network_stopTelnet()
{
	if (telnet_terminate()) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_stopTelnet_obj, mod_network_stopTelnet);

//---------------------------------------
STATIC mp_obj_t mod_network_TelnetMaxStack()
{
    return mp_obj_new_int(telnet_get_maxstack());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_TelnetMaxStack_obj, mod_network_TelnetMaxStack);

//--------------------------------------
STATIC mp_obj_t mod_network_stateTelnet()
{
	mp_obj_t tuple[3];
	char state[16] = {'\0'};

	int telnet_state = telnet_getstate();

	if (telnet_state == E_TELNET_STE_DISABLED) sprintf(state, "Disabled");
	else if (telnet_state == E_TELNET_STE_START) sprintf(state, "Starting");
	else if (telnet_state == E_TELNET_STE_LISTEN) sprintf(state, "Listen");
	else if (telnet_state == E_TELNET_STE_CONNECTED) sprintf(state, "Connected");
	else if (telnet_state == E_TELNET_STE_LOGGED_IN) sprintf(state, "Logged in");
	else if (telnet_state == -1) sprintf(state, "Not started");
	else sprintf(state, "Unknown");

	tuple[0] = mp_obj_new_int(telnet_state);
	tuple[1] = mp_obj_new_str(state, strlen(state));
	tuple[2] = get_listen_ip();

	return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_stateTelnet_obj, mod_network_stateTelnet);

//===============================================================
STATIC const mp_map_elem_t network_telnet_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start),	(mp_obj_t)&mod_network_startTelnet_obj },
    { MP_ROM_QSTR(MP_QSTR_pause),	(mp_obj_t)&mod_network_pauseTelnet_obj },
    { MP_ROM_QSTR(MP_QSTR_resume),	(mp_obj_t)&mod_network_resumeTelnet_obj },
    { MP_ROM_QSTR(MP_QSTR_stop),	(mp_obj_t)&mod_network_stopTelnet_obj },
    { MP_ROM_QSTR(MP_QSTR_status),	(mp_obj_t)&mod_network_stateTelnet_obj },
    { MP_ROM_QSTR(MP_QSTR_stack),	(mp_obj_t)&mod_network_TelnetMaxStack_obj }
};
STATIC MP_DEFINE_CONST_DICT(network_telnet_locals_dict, network_telnet_locals_dict_table);

//=========================================
const mp_obj_type_t network_telnet_type = {
    { &mp_type_type },
    .name = MP_QSTR_telnet,
    .locals_dict = (mp_obj_t)&network_telnet_locals_dict,
};

#endif


//=================================
#ifdef CONFIG_MICROPY_USE_FTPSERVER

#include "libs/ftp.h"

//-------------------------------------------------------------------------------------------------
STATIC mp_obj_t mod_network_startFtp(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t allowed_args[] = {
			{ MP_QSTR_user,         MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_password,     MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_buffsize,		MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = CONFIG_MICROPY_FTPSERVER_BUFFER_SIZE} },
			{ MP_QSTR_timeout,		MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = FTP_CMD_TIMEOUT_MS/1000} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if ((wifi_network_state < WIFI_STATE_STARTED) || (!wifi_is_started())) {
        #ifdef CONFIG_MICROPY_USE_ETHERNET
        if (!lan_eth_active) {
            ESP_LOGE("[Ftp_start]", "Network interface not started or not connected");
            return mp_const_false;
        }
        #else
        return mp_const_false;
        #endif
    }

    if (MP_OBJ_IS_STR(args[0].u_obj)) {
        snprintf(ftp_user, FTP_USER_PASS_LEN_MAX, mp_obj_str_get_str(args[0].u_obj));
    }
	else strcpy(ftp_user, FTP_DEF_USER);

	if (MP_OBJ_IS_STR(args[1].u_obj)) {
    	snprintf(ftp_pass, FTP_USER_PASS_LEN_MAX, mp_obj_str_get_str(args[1].u_obj));
    }
	else strcpy(ftp_pass, FTP_DEF_PASS);

    ftp_buff_size = args[2].u_int;
    if ((ftp_buff_size < 512) || (ftp_buff_size > 10240)) ftp_buff_size = CONFIG_MICROPY_FTPSERVER_BUFFER_SIZE;

    ftp_timeout = args[3].u_int * 1000;
    if ((ftp_timeout < 120000) || (ftp_timeout > 86400000)) ftp_timeout = FTP_CMD_TIMEOUT_MS;

    if (mp_thread_createFtpTask(FTP_STACK_LEN)) {
        ESP_LOGI("[Ftp]","user: %s; pass: %s; buffer: %d; timeout: %d\n", ftp_user, ftp_pass, ftp_buff_size, ftp_timeout);
    	return mp_const_true;
    }
	return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mod_network_startFtp_obj, 0, mod_network_startFtp);

//------------------------------------
STATIC mp_obj_t mod_network_pauseFtp()
{
	if (ftp_disable()) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_pauseFtp_obj, mod_network_pauseFtp);

//-------------------------------------
STATIC mp_obj_t mod_network_resumeFtp()
{
	if (ftp_enable()) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_resumeFtp_obj, mod_network_resumeFtp);

//-----------------------------------
STATIC mp_obj_t mod_network_stopFtp()
{
	if (ftp_terminate()) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_stopFtp_obj, mod_network_stopFtp);

//---------------------------------------
STATIC mp_obj_t mod_network_FtpMaxStack()
{
    return mp_obj_new_int(ftp_get_maxstack());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_FtpMaxStack_obj, mod_network_FtpMaxStack);

//------------------------------------
STATIC mp_obj_t mod_network_stateFtp()
{
	mp_obj_t tuple[5];
	char state[20] = {'\0'};
	int ftp_state, ftp_substate;

	int ftpstate = ftp_getstate();
	if (ftpstate >= 0) {
		ftp_state = ftpstate & 0xFF;
		ftp_substate = ftpstate >> 8;
	}
	else {
		ftp_state = ftpstate;
		ftp_substate = -1;
	}

	tuple[0] = mp_obj_new_int(ftp_state);
	tuple[1] = mp_obj_new_int(ftp_substate);

	if (ftp_state == E_FTP_STE_DISABLED) sprintf(state, "Disabled");
	else if (ftp_state == E_FTP_STE_START) sprintf(state, "Starting");
	else if (ftp_state == E_FTP_STE_READY) sprintf(state, "Ready");
	else if (ftp_state == E_FTP_STE_CONNECTED) sprintf(state, "Connected");
	else if (ftp_state == E_FTP_STE_END_TRANSFER) sprintf(state, "EndTransfer");
	else if (ftp_state == E_FTP_STE_CONTINUE_LISTING) sprintf(state, "Listing");
	else if (ftp_state == E_FTP_STE_CONTINUE_FILE_TX) sprintf(state, "Sending file");
	else if (ftp_state == E_FTP_STE_CONTINUE_FILE_RX) sprintf(state, "Receiving file");
	else if (ftp_state == -1) sprintf(state, "Not started");
	else if (ftp_state == -2) sprintf(state, "Busy!");
	else sprintf(state, "Unknown");
	tuple[2] = mp_obj_new_str(state, strlen(state));

	if (ftp_substate == E_FTP_STE_SUB_DISCONNECTED) sprintf(state, "Data: Disconnected");
	else if (ftp_substate == E_FTP_STE_SUB_LISTEN_FOR_DATA) sprintf(state, "Data: Listen");
	else if (ftp_substate == E_FTP_STE_SUB_DATA_CONNECTED) sprintf(state, "Data: Connected");
	else sprintf(state, "Unknown");
	tuple[3] = mp_obj_new_str(state, strlen(state));
	tuple[4] = get_listen_ip();

	return mp_obj_new_tuple(5, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_network_stateFtp_obj, mod_network_stateFtp);

//============================================================
STATIC const mp_map_elem_t network_ftp_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start),	(mp_obj_t)&mod_network_startFtp_obj },
    { MP_ROM_QSTR(MP_QSTR_pause),	(mp_obj_t)&mod_network_pauseFtp_obj },
    { MP_ROM_QSTR(MP_QSTR_resume),	(mp_obj_t)&mod_network_resumeFtp_obj },
    { MP_ROM_QSTR(MP_QSTR_stop),	(mp_obj_t)&mod_network_stopFtp_obj },
    { MP_ROM_QSTR(MP_QSTR_status),	(mp_obj_t)&mod_network_stateFtp_obj },
    { MP_ROM_QSTR(MP_QSTR_stack),	(mp_obj_t)&mod_network_FtpMaxStack_obj }
};
STATIC MP_DEFINE_CONST_DICT(network_ftp_locals_dict, network_ftp_locals_dict_table);

//======================================
const mp_obj_type_t network_ftp_type = {
    { &mp_type_type },
    .name = MP_QSTR_ftp,
    .locals_dict = (mp_obj_t)&network_ftp_locals_dict,
};

#endif


#ifdef CONFIG_MICROPY_USE_MDNS
extern const mp_obj_type_t mdns_type;
#endif

//--------------------------------------------------------------------
STATIC mp_obj_t esp_wlan_callback(size_t n_args, const mp_obj_t *args)
{
    if (n_args == 0) {
    	if (event_callback == NULL) return mp_const_false;
    	return mp_const_true;
    }

	if (wifi_mutex) xSemaphoreTake(wifi_mutex, 1000);
    if ((MP_OBJ_IS_FUN(args[0])) || (MP_OBJ_IS_METH(args[0]))) {
		event_callback = args[0];
    }
    else event_callback = NULL;
	if (wifi_mutex) xSemaphoreGive(wifi_mutex);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp_wlan_callback_obj, 0, 1, esp_wlan_callback);


//==============================================================
STATIC const mp_map_elem_t mp_module_network_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__),			MP_OBJ_NEW_QSTR(MP_QSTR_network) },
    { MP_OBJ_NEW_QSTR(MP_QSTR___init__),			(mp_obj_t)&esp_initialize_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_WLAN),				(mp_obj_t)&get_wlan_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_WLANcallback),		(mp_obj_t)&esp_wlan_callback_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_phy_mode),			(mp_obj_t)&esp_phy_mode_obj },
	#ifdef CONFIG_MICROPY_USE_ETHERNET
    { MP_OBJ_NEW_QSTR(MP_QSTR_LAN),					(mp_obj_t)&get_lan_obj },
	#endif
	#ifdef CONFIG_MICROPY_USE_MQTT
	{ MP_ROM_QSTR(MP_QSTR_mqtt),					(mp_obj_type_t *)&mqtt_type },
	#endif
	#ifdef CONFIG_MICROPY_USE_TELNET
	{ MP_ROM_QSTR(MP_QSTR_telnet),					(mp_obj_type_t *)&network_telnet_type },
	#endif
	#ifdef CONFIG_MICROPY_USE_FTPSERVER
	{ MP_ROM_QSTR(MP_QSTR_ftp),						(mp_obj_type_t *)&network_ftp_type },
	#endif
	#ifdef CONFIG_MICROPY_USE_MDNS
	{ MP_ROM_QSTR(MP_QSTR_mDNS),					(mp_obj_type_t *)&mdns_type },
	#endif

#if MODNETWORK_INCLUDE_CONSTANTS
    { MP_OBJ_NEW_QSTR(MP_QSTR_STA_IF),				MP_OBJ_NEW_SMALL_INT(WIFI_IF_STA)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_AP_IF),				MP_OBJ_NEW_SMALL_INT(WIFI_IF_AP)},

    { MP_OBJ_NEW_QSTR(MP_QSTR_MODE_11B),			MP_OBJ_NEW_SMALL_INT(WIFI_PROTOCOL_11B) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_MODE_11G),			MP_OBJ_NEW_SMALL_INT(WIFI_PROTOCOL_11G) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_MODE_11N),			MP_OBJ_NEW_SMALL_INT(WIFI_PROTOCOL_11N) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_AUTH_OPEN),			MP_OBJ_NEW_SMALL_INT(WIFI_AUTH_OPEN) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_AUTH_WEP),			MP_OBJ_NEW_SMALL_INT(WIFI_AUTH_WEP) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_AUTH_WPA_PSK),		MP_OBJ_NEW_SMALL_INT(WIFI_AUTH_WPA_PSK) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_AUTH_WPA2_PSK),		MP_OBJ_NEW_SMALL_INT(WIFI_AUTH_WPA2_PSK) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_AUTH_WPA_WPA2_PSK),	MP_OBJ_NEW_SMALL_INT(WIFI_AUTH_WPA_WPA2_PSK) },

	#ifdef CONFIG_MICROPY_USE_ETHERNET
    { MP_OBJ_NEW_QSTR(MP_QSTR_PHY_LAN8720),			MP_OBJ_NEW_SMALL_INT(PHY_LAN8720) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_PHY_TLK110),			MP_OBJ_NEW_SMALL_INT(PHY_TLK110) },
	#endif
#endif
};

STATIC MP_DEFINE_CONST_DICT(mp_module_network_globals, mp_module_network_globals_table);

const mp_obj_module_t mp_module_network = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_network_globals,
};
