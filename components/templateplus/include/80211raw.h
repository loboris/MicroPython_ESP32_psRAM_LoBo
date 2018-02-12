/*
 * 80211raw.h
 *
 *  Created on: Feb 9, 2018
 *      Author: michele
 */

#ifndef _80211RAW_H_
#define _80211RAW_H_

#include "esp_system.h"
#include "esp_wifi.h"

// buffer: Raw IEEE 802.11 packet to send
// len: Length of IEEE 802.11 packet
// en_sys_seq: see https://github.com/espressif/esp-idf/blob/master/docs/api-guides/wifi.rst#wi-fi-80211-packet-send for details
esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

const void wifi_rawtx(const void *);
const void wifi_beacontx(char *[]);
const void wifi_msgtx(char *);

#endif /* _80211RAW_H_ */
