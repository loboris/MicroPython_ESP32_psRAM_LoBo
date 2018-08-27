/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
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

#ifndef MICROPY_INCLUDED_ESP32_MODNETWORK_H
#define MICROPY_INCLUDED_ESP32_MODNETWORK_H

#include "esp_wifi_types.h"
#include "esp_eth.h"
#include "tcpip_adapter.h"

#define WIFI_STATE_NOTINIT      -1
#define WIFI_STATE_INIT     	0
#define WIFI_STATE_STOPPED      1
#define WIFI_STATE_STARTED      2
#define MAX_ACTIVE_INTERFACES   3

enum { PHY_LAN8720, PHY_TLK110 };

typedef struct _wlan_if_obj_t {
    mp_obj_base_t base;
    int if_id;
    wifi_mode_t wifi_mode;
} wlan_if_obj_t;

typedef void (*wifi_sta_rx_probe_req_t)(const uint8_t *frame, int len, int rssi);
extern esp_err_t esp_wifi_set_sta_rx_probe_req(wifi_sta_rx_probe_req_t cb);

extern const mp_obj_type_t wlan_if_type;
tcpip_adapter_if_t tcpip_if[3];

#ifdef CONFIG_MICROPY_USE_ETHERNET
extern bool lan_eth_active;
//extern eth_phy_check_link_func lan_eth_link_func;
#endif

extern int wifi_network_state;
extern bool wifi_sta_isconnected;
extern bool wifi_sta_has_ipaddress;
extern bool wifi_sta_changed_ipaddress;
extern bool wifi_ap_isconnected;
extern bool wifi_ap_sta_isconnected;

int network_get_active_interfaces();
uint32_t network_hasip();
uint32_t network_has_staip();
void network_checkConnection();


#ifdef CONFIG_MICROPY_USE_ETHERNET
MP_DECLARE_CONST_FUN_OBJ_KW(get_lan_obj);
#endif

MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(esp_ifconfig_obj);

#endif
