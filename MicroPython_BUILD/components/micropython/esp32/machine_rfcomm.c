/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
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

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_RFCOMM

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "api/esp_bt_main.h"
#include "api/esp_gap_bt_api.h"
#include "api/esp_bt_device.h"
#include "api/esp_spp_api.h"

#include "machine_uart.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "modmachine.h"

#define SPP_TAG "RFCOMM"
#define RFCOMM_CB_TYPE_DATA       1
#define RFCOMM_CB_TYPE_PATTERN    2
#define RFCOMM_CB_TYPE_STATUS     3
#define RFCOMM_MAX_CLIENTS        8

typedef struct _rfcomm_cb_obj_t {
    int data_cb_size;
    uint8_t pattern[16];
    uint8_t pattern_len;
    uint32_t *data_cb;
    uint32_t *pattern_cb;
    uint8_t lineend[3];
} rfcomm_cb_obj_t;

typedef struct _rfcomm_client_t {
    uint8_t client_btaddr[ESP_BD_ADDR_LEN];
    uint32_t handle;
    uint32_t wr_handle;
    uint16_t size;
    uint16_t iget;
    uint16_t iput;
    rfcomm_cb_obj_t cb;
    uint8_t *buf;
} rfcomm_client_t;

typedef struct _machine_rfcomm_obj_t {
    mp_obj_base_t base;
    bool init;
    int8_t channel;
    int8_t connected;
    esp_spp_sec_t sec_mask;
    esp_spp_role_t role;
    char server_name[32];
    char device_name[32];
    rfcomm_cb_obj_t cb;
    uint16_t timeout;       // timeout waiting for first char (in ms)
    uint16_t buffer_size;
    uint32_t *status_cb;
} machine_rfcomm_obj_t;

static machine_rfcomm_obj_t *rfcomm_obj = NULL;
static QueueHandle_t rfcomm_mutex = NULL;
static bool bt_controller_is_init = false;
static rfcomm_client_t * clients[RFCOMM_MAX_CLIENTS] = {NULL};

static const char* const rfcomm_events[] = {
        "SPP initialized", // 0
        "SDP discovery complete", // 1
        "SPP client connection open", // 2
        "SPP connection closed, client disconnected", // 3
        "SPP server started", // 4
        "SPP client initiated a connection", // 5
        "SPP connection received data", // 6
        "SPP connection congestion status changed", // 7
        "SPP write operation complete", // 8
        "SPP connection open, client connected", // 9
        "Data received, buffer overflow", // 10
        "SPP server start failed", // 11
};

//-----------------------------------------------------------------
static int rfcomm_buf_get(uint8_t idx, uint8_t *dest, uint16_t len)
{
    if ((clients[idx] == NULL) || (clients[idx]->buf == NULL)) return -1;
    if (clients[idx]->iget == clients[idx]->iput) return -1; // input buffer empty

    int res = 0;
    for (int i=0; i<len; i++) {
        dest[i] = clients[idx]->buf[clients[idx]->iget++];
        res++;
        if (clients[idx]->iget == clients[idx]->iput) break;
    }
    // move the buffer and adjust the pointers
    memmove(clients[idx], clients[idx]->buf+res, clients[idx]->iput - res);
    clients[idx]->iget -= res;
    clients[idx]->iput -= res;

    return res;
}

//-------------------------------------------------------------------
static int rfcomm_buf_put(uint8_t idx, uint8_t *source, uint16_t len)
{
    if ((clients[idx] == NULL) || (clients[idx]->buf == NULL)) return 1;
    int res = 0;
    for (int i=0; i<len; i++) {
        if (clients[idx]->iput >= clients[idx]->size) return 1; // overflow
        clients[idx]->buf[clients[idx]->iput++] = source[i];
    }
    return res;
}

//--------------------------------------------------------------------------------------------------------------
static void _sched_callback(mp_obj_t function, int8_t client, int type, int iarglen, uint8_t *sarg, char *param)
{
    mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    if (carg == NULL) return;
    if (client >= 0) {
        if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_INT, client, NULL, NULL)) return;
    }
    else {
        if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_NONE, 0, NULL, NULL)) return;
    }
    if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_INT, type, NULL, NULL)) return;
    if (sarg) {
        if (iarglen == 0) {
            if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, strlen((char *)sarg), sarg, NULL)) return;
        }
        else {
            if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, iarglen, sarg, NULL)) return;
        }
    }
    else {
        if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_INT, iarglen, NULL, NULL)) return;
    }
    if (param) {
        if (!make_carg_entry(carg, 3, MP_SCHED_ENTRY_TYPE_STR, strlen(param), (uint8_t *)param, NULL)) return;
    }
    mp_sched_schedule(function, mp_const_none, carg);
}

//----------------------------------------------------------------------
static void _rfcomm_event_status(int8_t client, int nevent, char *param)
{
    if (rfcomm_obj->status_cb) {
        _sched_callback(rfcomm_obj->status_cb, client, nevent, 0, (uint8_t *)rfcomm_events[nevent], param);
    }
    else {
        if (param) {
            if (client >= 0) {
                ESP_LOGD(SPP_TAG, "[client_%d] %s (%s)", client, rfcomm_events[nevent], param);
            }
            else {
                ESP_LOGD(SPP_TAG, "%s (%s)", rfcomm_events[nevent], param);
            }
        }
        else {
            if (client >= 0) {
                ESP_LOGD(SPP_TAG, "[client_%d] %s", client, rfcomm_events[nevent]);
            }
            else {
                ESP_LOGD(SPP_TAG, "%s", rfcomm_events[nevent]);
            }
        }
    }
}

//============================================================================
static void rfcomm_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (rfcomm_obj == NULL) return;
    int res;
    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
    switch (event) {
        case ESP_SPP_INIT_EVT:
        {
            esp_bt_dev_set_device_name((const char *)rfcomm_obj->device_name);
            esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
            esp_spp_start_srv(rfcomm_obj->sec_mask, rfcomm_obj->role, rfcomm_obj->channel, (const char *)rfcomm_obj->server_name);
            _rfcomm_event_status(-1, 0, NULL);
            break;
        }

        case ESP_SPP_DISCOVERY_COMP_EVT:
            _rfcomm_event_status(-1, 1, NULL);
            break;

        case ESP_SPP_OPEN_EVT:
            _rfcomm_event_status(-1, 2, NULL);
            break;

        case ESP_SPP_CLOSE_EVT:
        {
            int client = -1;
            for (int i=0; i<RFCOMM_MAX_CLIENTS; i++) {
                if ((clients[i]) && (clients[i]->handle == param->close.handle)) {
                    if (clients[i]->buf) free(clients[i]->buf);
                    free(clients[i]);
                    clients[i] = NULL;
                    client = i;
                    break;
                }
            }
            if (rfcomm_obj->connected) rfcomm_obj->connected--;
            _rfcomm_event_status(client, 3, NULL);
            break;
        }

        case ESP_SPP_START_EVT:
            if (param->start.status != ESP_SPP_SUCCESS) {
                _rfcomm_event_status(-1, 11, NULL);
                rfcomm_obj->channel++;
                if (rfcomm_obj->channel > 79) rfcomm_obj->channel = 1;
                ESP_LOGW(SPP_TAG, "Fail reason: %d, retry on channel %d", param->start.status, rfcomm_obj->channel);
                esp_spp_start_srv(rfcomm_obj->sec_mask, rfcomm_obj->role, rfcomm_obj->channel, (const char *)rfcomm_obj->server_name);
            }
            else {
                _rfcomm_event_status(-1, 4, NULL);
            }
            break;

        case ESP_SPP_CL_INIT_EVT:
            _rfcomm_event_status(-1, 5, NULL);
            break;

        case ESP_SPP_DATA_IND_EVT:
        {
            // Data received
            int cidx = -1;
            for (int i=0; i<RFCOMM_MAX_CLIENTS; i++) {
                if ((clients[i]) && (clients[i]->handle == param->data_ind.handle)) {
                    cidx = i;
                    break;
                }
            }
            if (cidx >= 0) {
                res = rfcomm_buf_put(cidx, param->data_ind.data, param->data_ind.len);
                if (res) _rfcomm_event_status(cidx, 10, NULL);
                else {
                    bool serviced = false;
                    if ((clients[cidx]->cb.data_cb) || (rfcomm_obj->cb.data_cb)) {
                        rfcomm_cb_obj_t *pcb = &rfcomm_obj->cb;
                        if (clients[cidx]->cb.data_cb) pcb = &clients[cidx]->cb;
                        if ((pcb->data_cb_size > 0) && (clients[cidx]->iput >= pcb->data_cb_size)) {
                            // ** callback on data length
                            uint8_t *dtmp = malloc(pcb->data_cb_size);
                            if (dtmp) {
                                rfcomm_buf_get(cidx, dtmp, pcb->data_cb_size);
                                _sched_callback(pcb->data_cb, cidx, RFCOMM_CB_TYPE_DATA, pcb->data_cb_size, dtmp, NULL);
                                free(dtmp);
                            }
                            serviced = true;
                        }
                    }
                    else if ((!serviced) && ((clients[cidx]->cb.pattern_cb) || (rfcomm_obj->cb.pattern_cb))) {
                        // ** callback on pattern received
                        rfcomm_cb_obj_t *pcb = &rfcomm_obj->cb;
                        if (clients[cidx]->cb.pattern_cb) pcb = &clients[cidx]->cb;
                        res = match_pattern(clients[cidx]->buf, clients[cidx]->iput, pcb->pattern, pcb->pattern_len);
                        if (res >= 0) {
                            // found, pull data, including pattern from buffer
                            uint8_t *dtmp = malloc(res + pcb->pattern_len);
                            if (dtmp) {
                                rfcomm_buf_get(cidx, dtmp, res + pcb->pattern_len);
                                _sched_callback(pcb->pattern_cb, cidx, RFCOMM_CB_TYPE_PATTERN, res, dtmp, NULL);
                                free(dtmp);
                            }
                        }
                    }
                    else {
                        ESP_LOGD(SPP_TAG, "[client %d] Data received: len=%d, in buffer=%d", cidx, param->data_ind.len, clients[cidx]->iput);
                    }
                }
            }
            break;
        }

        case ESP_SPP_CONG_EVT:
            _rfcomm_event_status(-1, 7, NULL);
            break;

        case ESP_SPP_WRITE_EVT:
            _rfcomm_event_status(-1, 8, NULL);
            break;

        case ESP_SPP_SRV_OPEN_EVT:
        {
            char btaddr[13] = {0};
            for (res = 0; res < 6; res++) {
                sprintf(btaddr+(res*2), "%02X", param->srv_open.rem_bda[res]);
            }
            // Add the new client to clients list
            int idx;
            for (idx=0; idx<RFCOMM_MAX_CLIENTS; idx++) {
                if (clients[idx] == NULL) {
                    clients[idx] = malloc(sizeof(rfcomm_client_t));
                    if (clients[idx] == NULL) {
                        esp_spp_disconnect(param->srv_open.handle);
                        ESP_LOGE(SPP_TAG, "Error allocating client's data for client %d [%s]", idx, btaddr);
                        break;
                    }
                    memset(clients[idx], 0, sizeof(rfcomm_client_t));
                    // Allocate the client's buffer
                    clients[idx]->buf = malloc(rfcomm_obj->buffer_size);
                    if (clients[idx]->buf == NULL) {
                        free(clients[idx]);
                        clients[idx] = NULL;
                        esp_spp_disconnect(param->srv_open.handle);
                        ESP_LOGE(SPP_TAG, "Error allocating data buffer for client %d [%s]", idx, btaddr);
                        break;
                    }
                    clients[idx]->size = rfcomm_obj->buffer_size;
                    clients[idx]->handle = param->srv_open.handle;
                    clients[idx]->wr_handle = param->write.handle;
                    memcpy(clients[idx]->client_btaddr, param->srv_open.rem_bda, ESP_BD_ADDR_LEN);
                    memcpy(clients[idx]->cb.lineend, rfcomm_obj->cb.lineend, sizeof(rfcomm_obj->cb.lineend));

                    _rfcomm_event_status(idx, 9, btaddr);
                    rfcomm_obj->connected++;
                    break;
                }
            }
            if (idx >= RFCOMM_MAX_CLIENTS) {
                esp_spp_disconnect(param->srv_open.handle);
                ESP_LOGW(SPP_TAG, "Maximal number of clients (%d) already connected", RFCOMM_MAX_CLIENTS);
            }
            break;
        }
        default:
            break;
    }
    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
}


//--------------------------------------------------------------------------------
static char *_rfcomm_read(uint8_t client, int timeout, char *lnend, char *lnstart)
{
    char *rdstr = NULL;
    int rdlen = -1;
    int minlen = strlen(lnend);
    if (lnstart) minlen += strlen(lnstart);

    if (timeout == 0) {
        if (rfcomm_mutex) {
            if (xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS) != pdTRUE) {
                return NULL;
            }
        }
        // check for minimal length
        if (clients[client]->iput < minlen) {
            if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
            return NULL;
        }
        while (1) {
            rdlen = match_pattern(clients[client]->buf, clients[client]->iput, (uint8_t *)lnend, strlen(lnend));
            if (rdlen >= 0) {
                // found, pull data, including pattern from buffer
                rdlen += 2;
                rdstr = calloc(rdlen+1, 1);
                if (rdstr) {
                    rfcomm_buf_get(client, (uint8_t *)rdstr, rdlen);
                    rdstr[rdlen] = 0;
                    if (lnstart) {
                        // Match beginning string
                        char *start_ptr = strstr(rdstr, lnstart);
                        if (start_ptr) {
                            if (start_ptr != rdstr) {
                                char *new_rdstr = strdup(start_ptr);
                                free(rdstr);
                                rdstr = new_rdstr;
                            }
                            break;
                        }
                        else {
                            free(rdstr);
                            rdstr = NULL;
                            rdlen = -1;
                            break;
                        }
                    }
                    else break;
                }
                else {
                    rdlen = -1;
                    break;
                }
            }
            else break;
        }
        if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
        if (rdlen < 0) return NULL;
    }
    else {
        // wait until lnend received or timeout
        int wait = timeout;
        int buflen = 0;
        mp_hal_set_wdt_tmo();
        while (wait > 0) {
            if (rfcomm_mutex) {
                if (xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS) != pdTRUE) {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    wait -= 10;
                    mp_hal_reset_wdt();
                    continue;
                }
            }
            if (buflen < clients[client]->iput) {
                // ** new data received, reset timeout
                buflen = clients[client]->iput;
                wait = timeout;
            }
            if (clients[client]->iput < minlen) {
                // ** too few characters received
                if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
                vTaskDelay(10 / portTICK_PERIOD_MS);
                wait -= 10;
                mp_hal_reset_wdt();
                continue;
            }

            while (1) {
                // * Check if lineend pattern is received
                rdlen = match_pattern(clients[client]->buf, clients[client]->iput, (uint8_t *)lnend, strlen(lnend));
                if (rdlen >= 0) {
                    rdlen += 2;
                    // * found, pull data, including pattern from buffer
                    rdstr = calloc(rdlen+1, 1);
                    if (rdstr) {
                        rfcomm_buf_get(client, (uint8_t *)rdstr, rdlen);
                        rdstr[rdlen] = 0;
                        if (lnstart) {
                            // * Find beginning of the sentence
                            char *start_ptr = strstr(rdstr, lnstart);
                            if (start_ptr) {
                                // === received string ending with lnend and starting with lnstart
                                if (start_ptr != rdstr) {
                                    char *new_rdstr = strdup(start_ptr);
                                    free(rdstr);
                                    rdstr = new_rdstr;
                                }
                                break;
                            }
                            else {
                                free(rdstr);
                                rdstr = NULL;
                                break;
                            }
                        }
                        else break; // === received string ending with lineend
                    }
                    else {
                        // error allocating buffer, finish
                        wait = 0;
                        break;
                    }
                }
                else break;
            }
            if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

            if (rdstr) break;
            if (wait > 0) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
                wait -= 10;
                mp_hal_reset_wdt();
            }
        }
    }
    return rdstr;
}

//-----------------------------------------------------------------------------------------------
STATIC int _machine_rfcomm_read(mp_obj_t self_in, uint8_t client, void *buf_in, mp_uint_t size) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // make sure we want at least 1 char
    if (size == 0) return 0;

    int bytes_read = 0;
    if (self->timeout == 0) {
        // just return the buffer content
        if (rfcomm_mutex) {
            if (xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS) != pdTRUE) {
                return 0;
            }
        }
        bytes_read = rfcomm_buf_get(client, (uint8_t *)buf_in, size);
        if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
        if (bytes_read < 0) bytes_read = 0;
    }
    else {
        // wait until data received or timeout
        mp_hal_set_wdt_tmo();
        int wait = self->timeout;
        MP_THREAD_GIL_EXIT();
        while (wait > 0) {
            if (rfcomm_mutex) {
                if (xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS) != pdTRUE) {
                    vTaskDelay(2 / portTICK_PERIOD_MS);
                    wait -= 2;
                    mp_hal_reset_wdt();
                    continue;
                }
            }
            if (clients[client]->iput < size) {
                if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
                vTaskDelay(2 / portTICK_PERIOD_MS);
                wait -= 2;
                mp_hal_reset_wdt();
                continue;
            }
            bytes_read = rfcomm_buf_get(client, (uint8_t *)buf_in, size);
            if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
            break;
        }
        MP_THREAD_GIL_ENTER();
    }

    return bytes_read;
}


/******************************************************************************/
// MicroPython bindings for UART


//-------------------------------------------------------------------
static void _check_rfcomm(machine_rfcomm_obj_t *self, int chk_client)
{
    if ((!self->init) || (rfcomm_obj == NULL)) {
        mp_raise_ValueError("RFCOMM not initialized");
    }

    if (chk_client >= -1) {
        if (self->connected <= 0) {
            mp_raise_ValueError("No client connected");
        }
        if (chk_client >= 0) {
            if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
            if (clients[chk_client] == NULL) {
                if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
                mp_raise_ValueError("Client not connected");
            }
            if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
        }
    }
}

//-------------------------------------------------------------------------------------------------
STATIC void machine_rfcomm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if ((!self->init) || (rfcomm_obj == NULL)) {
        mp_printf(print, "RFCOMM(%s: Deinitialized )", self->device_name);
        return;
    }

    char tmps[16] = {'\0'};
    int lnend_idx = 0;
    for (int i=0; i < strlen((char *)self->cb.lineend); i++) {
        if (self->cb.lineend[i] == 0) break;
        if ((self->cb.lineend[i] < 32) || (self->cb.lineend[i] > 126)) {
            if (self->cb.lineend[i] == '\r') {
                sprintf(tmps+lnend_idx, "\\r");
                lnend_idx += 2;
            }
            else if (self->cb.lineend[i] == '\n') {
                sprintf(tmps+lnend_idx, "\\n");
                lnend_idx += 2;
            }
            else {
                sprintf(tmps+lnend_idx, "\\x%2x", self->cb.lineend[i]);
                lnend_idx += 4;
            }
        }
        else {
            sprintf(tmps+lnend_idx, "%c", self->cb.lineend[i]);
            lnend_idx++;
        }
    }

    mp_printf(print, "RFCOMM(%s: channel=%d, timeout=%u, buf_size=%u, lineend=b'%s')", self->device_name, self->channel, self->timeout, self->buffer_size, tmps);

    if (self->connected) {
        mp_printf(print, "\n       Connected clients: %d (", self->connected);
        int nclients = 0;
        for (int client=0; client < RFCOMM_MAX_CLIENTS; client++) {
            if (clients[client]) {
                memset(tmps, 0, 16);
                for (int i = 0; i<6; i++) {
                    sprintf(tmps+(i*2), "%02X", clients[client]->client_btaddr[i]);
                }
                if (nclients > 0) mp_printf(print, ", ");
                nclients++;
                mp_printf(print, "%s [id=%d]", tmps, client);
            }
        }
        mp_printf(print, ")", tmps);
    }
    if (self->cb.data_cb) {
        mp_printf(print, "\n       data CB: True, on len: %d", self->cb.data_cb_size);
    }
    if (self->cb.pattern_cb) {
        char pattern[80] = {'\0'};
        for (int i=0; i<self->cb.pattern_len; i++) {
            if ((self->cb.pattern[i] >= 0x20) && (self->cb.pattern[i] < 0x7f)) pattern[strlen(pattern)] = self->cb.pattern[i];
            else sprintf(pattern+strlen(pattern), "\\x%02x", self->cb.pattern[i]);
        }
        mp_printf(print, "\n       pattern CB: True, pattern: b'%s'", pattern);
    }
    if (self->status_cb) {
        mp_printf(print, "\n       status CB: True");
    }
}

//----------------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_channel, ARG_device, ARG_server, ARG_timeout, ARG_buffer_size, ARG_lineend };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_channel,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_device,       MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_server,       MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_timeout,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_buffer_size,  MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 512} },
        { MP_QSTR_lineend,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    if (rfcomm_obj) {
        mp_raise_msg(&mp_type_OSError, "RFCOMM object already created, only one allowed");
    }

    if (rfcomm_mutex == NULL) {
        rfcomm_mutex = xSemaphoreCreateMutex();
    }

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if ((args[ARG_channel].u_int < 0) || (args[ARG_channel].u_int < 0)) {
        mp_raise_ValueError("invalid channel (0-79 allowed)");
    }
    // Create UART instance, set defaults
    machine_rfcomm_obj_t *self = m_new_obj(machine_rfcomm_obj_t);
    self->base.type = &machine_rfcomm_type;
    self->timeout = 0;
    self->cb.pattern[0] = 0;
    self->cb.pattern_len = 0;
    self->cb.data_cb = NULL;
    self->cb.pattern_cb = NULL;
    self->status_cb = NULL;
    self->cb.data_cb_size = 0;
    self->role = ESP_SPP_ROLE_SLAVE;
    self->sec_mask = ESP_SPP_SEC_NONE;
    self->init = false;
    self->channel = (uint8_t)args[ARG_channel].u_int;

    // Set names
    if (args[ARG_device].u_obj != mp_const_none) {
        const char *device = mp_obj_str_get_str(args[ARG_device].u_obj);
        memset(self->device_name, 0, sizeof(self->device_name));
        snprintf(self->device_name, sizeof(self->device_name), "%s", device);
    }
    else {
        sprintf(self->device_name, "MicroPython_RFCOMM");
    }

    if (args[ARG_server].u_obj != mp_const_none) {
        const char *device = mp_obj_str_get_str(args[ARG_server].u_obj);
        memset(self->server_name, 0, sizeof(self->server_name));
        snprintf(self->server_name, sizeof(self->server_name)-1, "%s", device);
    }
    else {
        sprintf(self->server_name, "ESP32_SPP_Server");
    }

    // set timeout
    if (args[ARG_timeout].u_int >= 0) self->timeout = args[ARG_timeout].u_int;

    // set line end
    sprintf((char *)self->cb.lineend, "\r\n");
    mp_buffer_info_t lnend_buff;
    mp_obj_type_t *lne_type = mp_obj_get_type(args[ARG_lineend].u_obj);
    if (lne_type->buffer_p.get_buffer != NULL) {
        int ret = lne_type->buffer_p.get_buffer(args[ARG_lineend].u_obj, &lnend_buff, MP_BUFFER_READ);
        if (ret == 0) {
            if ((lnend_buff.len > 0) && (lnend_buff.len < sizeof(self->cb.lineend))) {
                memset(self->cb.lineend, 0, sizeof(self->cb.lineend));
                memcpy(self->cb.lineend, lnend_buff.buf, lnend_buff.len);
            }
        }
    }

    // Set buffer size
    int bufsize = args[ARG_buffer_size].u_int;
    if (bufsize < 512) bufsize = 512;
    if (bufsize > 8192) bufsize = 8192;
    self->buffer_size = bufsize;

    self->init = true;

    rfcomm_obj = self;

    if (!bt_controller_is_init) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "BT controller initialization failed");
        }
        bt_controller_is_init = true;
    }
    if (esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "BT controller enable failed");
    }
    if (esp_bluedroid_init() != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Bluedroid initialization failed");
    }
    if (esp_bluedroid_enable() != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Bluedroid enable failed");
    }
    if (esp_spp_register_callback(rfcomm_spp_cb) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "BT event callback registration failed");
    }

    if (esp_spp_init(ESP_SPP_MODE_CB) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "BT SPP initialization failed");
    }

    return MP_OBJ_FROM_PTR(self);
}

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_client, ARG_timeout, ARG_buffer_size, ARG_lineend };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_client,                        MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_timeout,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_buffer_size,  MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_lineend,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _check_rfcomm(self, -2);

    // set timeout
    if (args[ARG_timeout].u_int >= 0) self->timeout = args[ARG_timeout].u_int;

    // set line end
    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
    rfcomm_cb_obj_t *pcb = &self->cb;
    if ((args[ARG_client].u_int >= 0) && (clients[args[ARG_client].u_int])) pcb = &clients[args[ARG_client].u_int]->cb;

    mp_buffer_info_t lnend_buff;
    mp_obj_type_t *lne_type = mp_obj_get_type(args[ARG_lineend].u_obj);
    if (lne_type->buffer_p.get_buffer != NULL) {
        int ret = lne_type->buffer_p.get_buffer(args[ARG_lineend].u_obj, &lnend_buff, MP_BUFFER_READ);
        if (ret == 0) {
            if ((lnend_buff.len > 0) && (lnend_buff.len < sizeof(pcb->lineend))) {
                memset(pcb->lineend, 0, sizeof(pcb->lineend));
                memcpy(pcb->lineend, lnend_buff.buf, lnend_buff.len);
            }
        }
    }

    // Set buffer size
    if ((args[ARG_buffer_size].u_int >= 512) && (args[ARG_buffer_size].u_int <= 8192)) {
        if (args[ARG_buffer_size].u_int != self->buffer_size) {
            self->buffer_size = args[ARG_buffer_size].u_int;
        }
    }
    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_rfcomm_init_obj, 1, machine_rfcomm_init);

//-------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_deinit(mp_obj_t self_in) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->init) {
        if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
        for (int i=0; i<RFCOMM_MAX_CLIENTS; i++) {
            if (clients[i]) {
                if (clients[i]->buf) free(clients[i]->buf);
                free(clients[i]);
                clients[i] = NULL;
                break;
            }
        }
        rfcomm_obj = NULL;
        if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

        esp_spp_deinit();
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        //esp_bt_controller_deinit();

        self->init = false;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_rfcomm_deinit_obj, machine_rfcomm_deinit);

//---------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_any(mp_obj_t self_in, mp_obj_t client) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int idx = mp_obj_get_int(client);

    _check_rfcomm(self, idx);

    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
    int res = clients[idx]->iput;
    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

    return MP_OBJ_NEW_SMALL_INT(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_rfcomm_any_obj, machine_rfcomm_any);

//-------------------------------------------------------------------
mp_obj_t machine_rfcomm_readln(size_t n_args, const mp_obj_t *args) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    int idx = mp_obj_get_int(args[1]);
    _check_rfcomm(self, idx);

    int timeout = self->timeout;
    if (n_args > 1) timeout = mp_obj_get_int(args[2]);

    const char *startstr = NULL;
    if (n_args > 3) startstr = mp_obj_str_get_str(args[3]);

    MP_THREAD_GIL_EXIT();
    char *rdstr = _rfcomm_read(idx, timeout, (char *)clients[idx]->cb.lineend, (char *)startstr);
    MP_THREAD_GIL_ENTER();

    if (rdstr == NULL) return mp_const_none;
    mp_obj_t res_str = mp_obj_new_str((const char *)rdstr, strlen(rdstr));
    if (rdstr != NULL) free(rdstr);
    return res_str;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_rfcomm_readln_obj, 2, 4, machine_rfcomm_readln);

//-------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_read(mp_obj_t self_in, mp_obj_t client, mp_obj_t len_in)
{
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int idx = mp_obj_get_int(client);

    _check_rfcomm(self, idx);

    vstr_t vstr;
    int len = mp_obj_get_int(len_in);
    vstr_init_len(&vstr, len);

    int res = _machine_rfcomm_read(self, idx, vstr.buf, len);

    if (res <= 0) return mp_const_empty_bytes;

    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);

}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_rfcomm_read_obj, machine_rfcomm_read);

//------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_readinto(mp_obj_t self_in, mp_obj_t client, mp_obj_t buf_out)
{
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int idx = mp_obj_get_int(client);

    _check_rfcomm(self, idx);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_out, &bufinfo, MP_BUFFER_WRITE);

    int res = _machine_rfcomm_read(self, idx, bufinfo.buf, bufinfo.len);

    if (res <= 0) return MP_OBJ_NEW_SMALL_INT(0);

    return MP_OBJ_NEW_SMALL_INT(res);

}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_rfcomm_readinto_obj, machine_rfcomm_readinto);

//----------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_write(mp_obj_t self_in, mp_obj_t client, mp_obj_t buf_in) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int idx = mp_obj_get_int(client);

    _check_rfcomm(self, idx);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    int res = -1;
    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
    res = esp_spp_write(clients[idx]->wr_handle, bufinfo.len, (uint8_t *)bufinfo.buf);
    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

    if (res != ESP_OK) return MP_OBJ_NEW_SMALL_INT(0);
    return MP_OBJ_NEW_SMALL_INT(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_rfcomm_write_obj, machine_rfcomm_write);

//----------------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_disconnect(mp_obj_t self_in, mp_obj_t client) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int idx = mp_obj_get_int(client);

    _check_rfcomm(self, idx);

    int res = -1;
    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
    res = esp_spp_disconnect(clients[idx]->handle);
    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

    return (res == ESP_OK) ? mp_const_true : mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_rfcomm_disconnect_obj, machine_rfcomm_disconnect);

//----------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_connected(mp_obj_t self_in) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    _check_rfcomm(self, -2);

    mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int(self->connected);

    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);

    if (self->connected) {
        char btaddr[13] = {0};
        mp_obj_t clients_tuple[self->connected];
        mp_obj_t client_tuple[2];
        int client = 0;
        for (int idx=0; idx<RFCOMM_MAX_CLIENTS; idx++) {
            if (clients[idx]) {
                for (int i = 0; i<6; i++) {
                    sprintf(btaddr+(i*2), "%02X", clients[idx]->client_btaddr[i]);
                }
                client_tuple[0] = mp_obj_new_int(idx);
                client_tuple[1] = mp_obj_new_str(btaddr, strlen(btaddr));
                clients_tuple[client++] = mp_obj_new_tuple(2, client_tuple);
            }
        }
        tuple[1] = mp_obj_new_tuple(self->connected, clients_tuple);
    }
    else tuple[1] = mp_const_none;

    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_rfcomm_connected_obj, machine_rfcomm_connected);

//--------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_channel(mp_obj_t self_in) {
    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(self_in);

    _check_rfcomm(self, -2);

    return MP_OBJ_NEW_SMALL_INT(self->channel);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_rfcomm_channel_obj, machine_rfcomm_channel);

//-------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rfcomm_callback(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_type, ARG_func, ARG_client, ARG_pattern, ARG_datalen };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_type,         MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_func,         MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_client,       MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -2 } },
        { MP_QSTR_pattern,      MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_data_len,     MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
    };

    machine_rfcomm_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int client = args[ARG_client].u_int;
    if ((client != -2) && ((client < 0) || (client >= RFCOMM_MAX_CLIENTS))) {
        mp_raise_ValueError("invalid client id");
    }
    _check_rfcomm(self, client);

    int datalen = -1;
    mp_buffer_info_t pattern_buff;
    int cbtype = args[ARG_type].u_int;
    if ((cbtype != RFCOMM_CB_TYPE_DATA) && (cbtype != RFCOMM_CB_TYPE_PATTERN) && (cbtype != RFCOMM_CB_TYPE_STATUS)) {
        mp_raise_ValueError("invalid callback type");
    }

    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
    rfcomm_cb_obj_t *pcb = &self->cb;
    if ((args[ARG_client].u_int >= 0) && (cbtype != RFCOMM_CB_TYPE_STATUS) && (clients[args[ARG_client].u_int])) pcb = &clients[args[ARG_client].u_int]->cb;

    if ((!MP_OBJ_IS_FUN(args[ARG_func].u_obj)) && (!MP_OBJ_IS_METH(args[ARG_func].u_obj))) {
        // === CB function not given, disable callback ===
        switch(cbtype) {
            case RFCOMM_CB_TYPE_DATA:
                pcb->data_cb = NULL;
                pcb->data_cb_size = 0;
                break;
            case RFCOMM_CB_TYPE_PATTERN:
                pcb->pattern_cb = NULL;
                pcb->pattern[0] = 0;
                pcb->pattern_len = 0;
                break;
            case RFCOMM_CB_TYPE_STATUS:
                self->status_cb = NULL;
                break;
            default:
                break;
        }
        if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
        return mp_const_none;
    }

    // Get and check callback parameters
    switch(cbtype) {
        case RFCOMM_CB_TYPE_DATA:
            if ((args[ARG_datalen].u_int <= 0) || (args[ARG_datalen].u_int >= self->buffer_size)) {
                if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
                mp_raise_ValueError("invalid data length");
            }
            datalen = args[ARG_datalen].u_int;
            break;
        case RFCOMM_CB_TYPE_PATTERN:
            {
                bool has_pattern = false;
                mp_obj_type_t *type = mp_obj_get_type(args[ARG_pattern].u_obj);
                if (type->buffer_p.get_buffer != NULL) {
                    int ret = type->buffer_p.get_buffer(args[ARG_pattern].u_obj, &pattern_buff, MP_BUFFER_READ);
                    if (ret == 0) {
                        if ((pattern_buff.len > 0) && (pattern_buff.len <= sizeof(pcb->pattern))) has_pattern = true;
                    }
                }
                if (!has_pattern) {
                    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);
                    mp_raise_ValueError("invalid pattern");
                }
            }
            break;
        default:
            break;
    }

    // Set the callback
    if (rfcomm_mutex) xSemaphoreTake(rfcomm_mutex, 200 / portTICK_PERIOD_MS);
    switch(cbtype) {
        case RFCOMM_CB_TYPE_DATA:
            pcb->data_cb_size = datalen;
            pcb->data_cb = args[ARG_func].u_obj;
            break;
        case RFCOMM_CB_TYPE_PATTERN:
            memcpy(pcb->pattern, pattern_buff.buf, pattern_buff.len);
            pcb->pattern_len = pattern_buff.len;
            pcb->pattern_cb = args[ARG_func].u_obj;
            break;
        case RFCOMM_CB_TYPE_STATUS:
            self->status_cb = args[ARG_func].u_obj;
            break;
        default:
            break;
    }
    if (rfcomm_mutex) xSemaphoreGive(rfcomm_mutex);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_rfcomm_callback_obj, 2, machine_rfcomm_callback);


//===================================================================
STATIC const mp_rom_map_elem_t machine_rfcomm_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),            MP_ROM_PTR(&machine_rfcomm_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),          MP_ROM_PTR(&machine_rfcomm_deinit_obj) },

    { MP_ROM_QSTR(MP_QSTR_any),             MP_ROM_PTR(&machine_rfcomm_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),            MP_ROM_PTR(&machine_rfcomm_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline),        MP_ROM_PTR(&machine_rfcomm_readln_obj) },
    { MP_ROM_QSTR(MP_QSTR_readln),          MP_ROM_PTR(&machine_rfcomm_readln_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto),        MP_ROM_PTR(&machine_rfcomm_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),           MP_ROM_PTR(&machine_rfcomm_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_callback),        MP_ROM_PTR(&machine_rfcomm_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_connected),       MP_ROM_PTR(&machine_rfcomm_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect),      MP_ROM_PTR(&machine_rfcomm_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel),         MP_ROM_PTR(&machine_rfcomm_channel_obj) },

    // class constants
    { MP_ROM_QSTR(MP_QSTR_CBTYPE_DATA),     MP_ROM_INT(RFCOMM_CB_TYPE_DATA) },
    { MP_ROM_QSTR(MP_QSTR_CBTYPE_PATTERN),  MP_ROM_INT(RFCOMM_CB_TYPE_PATTERN) },
    { MP_ROM_QSTR(MP_QSTR_CBTYPE_STATUS),   MP_ROM_INT(RFCOMM_CB_TYPE_STATUS) },
};
STATIC MP_DEFINE_CONST_DICT(machine_rfcomm_locals_dict, machine_rfcomm_locals_dict_table);


//=========================================
const mp_obj_type_t machine_rfcomm_type = {
    { &mp_type_type },
    .name = MP_QSTR_RFCOMM,
    .print = machine_rfcomm_print,
    .make_new = machine_rfcomm_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_rfcomm_locals_dict,
};

#endif
