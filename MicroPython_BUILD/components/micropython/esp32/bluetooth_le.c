/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
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


// Free RTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Generic
#include <string.h>
#include <unistd.h>

// MicroPython
#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime0.h"

// IDF
#include "esp_bt.h"
#include "api/esp_bt_main.h"
#include "api/esp_gap_ble_api.h"
#include "api/esp_gatts_api.h"
#include "api/esp_gattc_api.h"

//extern bool bluetooth_enabled;

static bool bluetooth_enabled = false;

#define EVENT_DEBUG

#ifdef EVENT_DEBUG
#   define EVENT_DEBUG_GATTC
#   define EVENT_DEBUG_GATTS
#   define EVENT_DEBUG_GAP
#endif
#define MSEC_0_625_TO_UNIT(time) ((time) * 1000 / (625))

#define MALLOC(x) pvPortMalloc(x)
#define FREE(x) vPortFree(x)
#define UUID_LEN 16
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define BAD_UUID(name) mp_raise_ValueError(name "must be bytearray(" TOSTRING(UUID_LEN) ")");
#define BAD_PEER(name) mp_raise_ValueError(name "must be bytearray(" TOSTRING(ESP_BD_ADDR_LEN) ")");
#define MP_OBJ_IS_BYTEARRAY_OR_BYTES(O) (MP_OBJ_IS_TYPE(O, &mp_type_bytes) || MP_OBJ_IS_TYPE(O, &mp_type_bytearray))
#define NETWORK_BLUETOOTH_DEBUG_PRINTF(args...) printf(args)

#define CALLBACK_QUEUE_SIZE 10

typedef enum {

    // GATTS
    BTLE_GATTS_CONNECT,
    BTLE_GATTS_DISCONNECT,

    BTLE_GATTS_CREATE,
    BTLE_GATTS_START,
    BTLE_GATTS_STOP,
    BTLE_GATTS_ADD_CHAR, // Add char
    BTLE_GATTS_ADD_CHAR_DESCR, // Add descriptor

    // GAP / GATTC events
    BTLE_GATTC_SCAN_RES,
    BTLE_GATTC_SCAN_CMPL, // Found GATT servers
    BTLE_GATTC_SEARCH_RES, // Found GATTS services

    BTLE_GATTC_GET_CHAR,
    BTLE_GATTC_GET_DESCR,

    BTLE_GATTC_OPEN,
    BTLE_GATTC_CLOSE,

    // characteristic events
    BTLE_READ,
    BTLE_WRITE,
    BTLE_NOTIFY,

} btle_event_t;

typedef struct {
    btle_event_t event;
    union {

        struct {
            uint16_t                handle;
            esp_gatt_srvc_id_t      service_id;
        } gatts_create;
        /*

           struct {
           uint16_t                service_handle;
           } gatts_start_stop;

           struct {
           uint16_t                service_handle;
           uint16_t                handle;
           esp_bt_uuid_t           uuid;
           } gatts_add_char_descr;

           struct {
           uint16_t                conn_id;
           esp_bd_addr_t           bda;
           } gatts_connect_disconnect;

           struct {
           esp_bd_addr_t           bda;
           uint8_t*                adv_data; // Need to free this!
           uint8_t                 adv_data_len;
           int                     rssi;
           } gattc_scan_res;

           struct {
           uint16_t                conn_id;
           esp_gatt_id_t      service_id;
           } gattc_search_res;

           struct {
           uint16_t                conn_id;
           esp_gatt_srvc_id_t      service_id;
           esp_gatt_id_t           char_id;
           esp_gatt_char_prop_t    props;
           } gattc_get_char;

           struct {
           uint16_t                conn_id;
           esp_gatt_srvc_id_t      service_id;
           esp_gatt_id_t           char_id;
           esp_gatt_id_t           descr_id;
           } gattc_get_descr;

           struct {
           uint16_t                conn_id;
           uint16_t                mtu;
           esp_bd_addr_t           bda;

           } gattc_open_close;

           struct {
           uint16_t                conn_id;
           uint16_t                handle;
           uint32_t                trans_id;
           bool                    need_rsp;
           } read;

           struct {
           uint16_t                conn_id;
           uint16_t                handle;
           uint32_t                trans_id;
           bool                    need_rsp;

        // Following fields _must_
        // come after the first four above,
        // which _must_ match the read struct

        uint8_t*                value; // Need to free this!
        size_t                  value_len;
        } write;

        struct {
            uint16_t                conn_id;
            esp_bd_addr_t           remote_bda;
            uint16_t                handle;

            bool                    need_rsp;

            uint8_t*                value; // Need to free this!
            size_t                  value_len;
        } notify;
        */
    };

} callback_data_t;

typedef struct {
    uint8_t*                value; // Need to free this!
    size_t                  value_len;
} read_write_data_t;

const mp_obj_type_t btle_type;

STATIC SemaphoreHandle_t item_mut;
STATIC QueueHandle_t callback_q = NULL;
STATIC QueueHandle_t read_write_q = NULL;

STATIC void dumpBuf(const uint8_t *buf, size_t len) {
    while (len--) {
        printf("%02X ", *buf++);
    }
}

typedef struct {
    mp_obj_base_t           base;
    enum {
        BTLE_STATE_DEINIT,
        BTLE_STATE_INIT
    }                       state;
    bool                    advertising;
    bool                    scanning;
    bool                    gatts_connected;

    uint16_t                conn_id;
    esp_gatt_if_t           gatts_interface;
    esp_gatt_if_t           gattc_interface;

    esp_ble_adv_params_t    adv_params;
    esp_ble_adv_data_t      adv_data;

    mp_obj_t                services;       // GATTS, implemented as a list

    mp_obj_t                connections;    // GATTC, implemented as a list

    mp_obj_t                callback;
    mp_obj_t                callback_userdata;
} btle_obj_t;

// Singleton
STATIC btle_obj_t* btle_singleton = NULL;

STATIC btle_obj_t* btle_get_singleton() {

    if (btle_singleton == NULL) {
        btle_singleton = m_new_obj(btle_obj_t);

        btle_singleton->base.type = &btle_type;

        btle_singleton->state = BTLE_STATE_DEINIT;
        btle_singleton->advertising = false;
        btle_singleton->scanning = false;
        btle_singleton->gatts_connected = false;
        btle_singleton->conn_id = 0;
        btle_singleton->gatts_interface = ESP_GATT_IF_NONE;
        btle_singleton->gattc_interface = ESP_GATT_IF_NONE;
        btle_singleton->adv_params.adv_int_min = 1280 * 1.6;
        btle_singleton->adv_params.adv_int_max = 1280 * 1.6;
        btle_singleton->adv_params.adv_type = ADV_TYPE_IND;
        btle_singleton->adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        btle_singleton->adv_params.peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
        btle_singleton->adv_params.channel_map = ADV_CHNL_ALL;
        btle_singleton->adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
        btle_singleton->adv_data.set_scan_rsp = false;
        btle_singleton->adv_data.include_name = false;
        btle_singleton->adv_data.include_txpower = false;
        btle_singleton->adv_data.min_interval = 1280 * 1.6;
        btle_singleton->adv_data.max_interval = 1280 * 1.6;
        btle_singleton->adv_data.appearance = 0;
        btle_singleton->adv_data.p_manufacturer_data = NULL;
        btle_singleton->adv_data.manufacturer_len = 0;
        btle_singleton->adv_data.p_service_data = NULL;
        btle_singleton->adv_data.service_data_len = 0;
        btle_singleton->adv_data.p_service_uuid = 0;
        btle_singleton->adv_data.service_uuid_len = 0;
        btle_singleton->adv_data.flag = 0;

        btle_singleton->callback = mp_const_none;
        btle_singleton->callback_userdata = mp_const_none;

        btle_singleton->services = mp_obj_new_list(0, NULL);
        btle_singleton->connections = mp_obj_new_list(0, NULL);
        memset(btle_singleton->adv_params.peer_addr, 0, sizeof(btle_singleton->adv_params.peer_addr));
    }
    return btle_singleton;
}

#if CONFIG_GATTS_ENABLE
STATIC void btle_gatts_event_handler(
        esp_gatts_cb_event_t event,
        esp_gatt_if_t gatts_if,
        esp_ble_gatts_cb_param_t *param) {

    btle_obj_t* bluetooth = btle_get_singleton();
    if (bluetooth->state != BTLE_STATE_INIT) {
        return;
    }

#ifdef EVENT_DEBUG_GATTS
    //gatts_event_dump(event, gatts_if, param);
#endif
}
#endif

#if CONFIG_GATTC_ENABLE
STATIC void btle_gattc_event_handler(
        esp_gattc_cb_event_t event,
        esp_gatt_if_t gattc_if,
        esp_ble_gattc_cb_param_t *param) {

    btle_obj_t* bluetooth = btle_get_singleton();
    if (bluetooth->state != BTLE_STATE_INIT) {
        return;
    }
#ifdef EVENT_DEBUG_GATTC
    //gattc_event_dump(event, gattc_if, param);
#endif
}
#endif


#ifdef EVENT_DEBUG_GAP

typedef struct {
    uint8_t id;
    const char * name;
} gatt_status_name_t;

STATIC const gatt_status_name_t gatt_status_names[] = {
    {0x00, "OK"},
    {0x01, "INVALID_HANDLE"},
    {0x02, "READ_NOT_PERMIT"},
    {0x03, "WRITE_NOT_PERMIT"},
    {0x04, "INVALID_PDU"},
    {0x05, "INSUF_AUTHENTICATION"},
    {0x06, "REQ_NOT_SUPPORTED"},
    {0x07, "INVALID_OFFSET"},
    {0x08, "INSUF_AUTHORIZATION"},
    {0x09, "PREPARE_Q_FULL"},
    {0x0a, "NOT_FOUND"},
    {0x0b, "NOT_LONG"},
    {0x0c, "INSUF_KEY_SIZE"},
    {0x0d, "INVALID_ATTR_LEN"},
    {0x0e, "ERR_UNLIKELY"},
    {0x0f, "INSUF_ENCRYPTION"},
    {0x10, "UNSUPPORT_GRP_TYPE"},
    {0x11, "INSUF_RESOURCE"},
    {0x80, "NO_RESOURCES"},
    {0x81, "INTERNAL_ERROR"},
    {0x82, "WRONG_STATE"},
    {0x83, "DB_FULL"},
    {0x84, "BUSY"},
    {0x85, "ERROR"},
    {0x86, "CMD_STARTED"},
    {0x87, "ILLEGAL_PARAMETER"},
    {0x88, "PENDING"},
    {0x89, "AUTH_FAIL"},
    {0x8a, "MORE"},
    {0x8b, "INVALID_CFG"},
    {0x8c, "SERVICE_STARTED"},
    {0x8d, "ENCRYPED_NO_MITM"},
    {0x8e, "NOT_ENCRYPTED"},
    {0x8f, "CONGESTED"},
    {0x90, "DUP_REG"},
    {0x91, "ALREADY_OPEN"},
    {0x92, "CANCEL"},
    {0xfd, "CCC_CFG_ERR"},
    {0xfe, "PRC_IN_PROGRESS"},
    {0xff, "OUT_OF_RANGE"},
    {0x00,  NULL},
};

#define PRINT_STATUS(STATUS) { \
    bool found = false; \
    NETWORK_BLUETOOTH_DEBUG_PRINTF("status = %02X / ", (STATUS)); \
    for(int i = 0; gatt_status_names[i].name != NULL; i++) { \
        if (gatt_status_names[i].id == (STATUS)) { \
            found = true; \
            NETWORK_BLUETOOTH_DEBUG_PRINTF("%s", gatt_status_names[i].name); \
            break; \
        } \
    } \
    if (!found) { \
        NETWORK_BLUETOOTH_DEBUG_PRINTF("???"); \
    }  \
}
STATIC void gap_event_dump(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    const char * event_names[] = {
        "ADV_DATA_SET_COMPLETE",
        "SCAN_RSP_DATA_SET_COMPLETE",
        "SCAN_PARAM_SET_COMPLETE",
        "SCAN_RESULT",
        "ADV_DATA_RAW_SET_COMPLETE",
        "SCAN_RSP_DATA_RAW_SET_COMPLETE",
        "ADV_START_COMPLETE",
        "SCAN_START_COMPLETE",
        "AUTH_CMPL",
        "KEY",
        "SEC_REQ",
        "PASSKEY_NOTIF",
        "PASSKEY_REQ",
        "OOB_REQ",
        "LOCAL_IR",
        "LOCAL_ER",
        "NC_REQ",
        "ADV_STOP_COMPLETE",
        "SCAN_STOP_COMPLETE",
        "SET_STATIC_RAND_ADDR_EVT",
        "UPDATE_CONN_PARAMS_EVT",
        "SET_PKT_LENGTH_COMPLETE_EVT"
    };

    NETWORK_BLUETOOTH_DEBUG_PRINTF("network_bluetooth_gap_event_handler(event = %02X / %s", event, event_names[event]);
    NETWORK_BLUETOOTH_DEBUG_PRINTF(", param = (");
    switch (event) {
        case ESP_GAP_BLE_EVT_MAX:
            break;

        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        case ESP_GAP_BLE_CLEAR_BOND_DEV_COMPLETE_EVT:
        case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
            {
                PRINT_STATUS(param->adv_data_cmpl.status);
            }
            break;


        case ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT:
            {
                PRINT_STATUS(param->adv_data_cmpl.status);
                NETWORK_BLUETOOTH_DEBUG_PRINTF(
                        ", rssi = %d, bda = %02X:%02X:%02X:%02X:%02X:%02X",
                        param->read_rssi_cmpl.rssi,
                        param->read_rssi_cmpl.remote_addr[0],
                        param->read_rssi_cmpl.remote_addr[1],
                        param->read_rssi_cmpl.remote_addr[2],
                        param->read_rssi_cmpl.remote_addr[3],
                        param->read_rssi_cmpl.remote_addr[4],
                        param->read_rssi_cmpl.remote_addr[5]);
            }
            break;

        case ESP_GAP_BLE_ADD_WHITELIST_COMPLETE_EVT:
            {
                PRINT_STATUS(param->adv_data_cmpl.status);
                NETWORK_BLUETOOTH_DEBUG_PRINTF(
                        ", wl_opration = %s",
                        param->update_whitelist_cmpl.wl_opration == ESP_BLE_WHITELIST_ADD
                        ? "ADD" : "REMOVE");
            }
            break;

        case ESP_GAP_BLE_GET_BOND_DEV_COMPLETE_EVT:
            {
                PRINT_STATUS(param->adv_data_cmpl.status);
                NETWORK_BLUETOOTH_DEBUG_PRINTF(
                        ", dev_num = %d, bond_dev = <print not impl>",
                        param->get_bond_dev_cmpl.dev_num);
            }
            break;
        case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
            {
                PRINT_STATUS(param->adv_data_cmpl.status);
                NETWORK_BLUETOOTH_DEBUG_PRINTF(
                        ", bda = %02X:%02X:%02X:%02X:%02X:%02X",
                        param->remove_bond_dev_cmpl.bd_addr[0],
                        param->remove_bond_dev_cmpl.bd_addr[1],
                        param->remove_bond_dev_cmpl.bd_addr[2],
                        param->remove_bond_dev_cmpl.bd_addr[3],
                        param->remove_bond_dev_cmpl.bd_addr[4],
                        param->remove_bond_dev_cmpl.bd_addr[5]);
            }
            break;

        case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
            {
                PRINT_STATUS(param->pkt_data_lenth_cmpl.status);
                NETWORK_BLUETOOTH_DEBUG_PRINTF(
                        ", rx_len = %04X"
                        ", tx_len = %04X",
                        param->pkt_data_lenth_cmpl.params.rx_len,
                        param->pkt_data_lenth_cmpl.params.tx_len
                        );

            }
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            {
                PRINT_STATUS(param->update_conn_params.status);
                NETWORK_BLUETOOTH_DEBUG_PRINTF(
                        ", bda = %02X:%02X:%02X:%02X:%02X:%02X"
                        ", min_int = %04X"
                        ", max_int = %04X"
                        ", latency = %04X"
                        ", conn_int = %04X"
                        ", timeout = %04X",
                        param->update_conn_params.bda[0],
                        param->update_conn_params.bda[1],
                        param->update_conn_params.bda[2],
                        param->update_conn_params.bda[3],
                        param->update_conn_params.bda[4],
                        param->update_conn_params.bda[5],
                        param->update_conn_params.min_int,
                        param->update_conn_params.max_int,
                        param->update_conn_params.latency,
                        param->update_conn_params.conn_int,
                        param->update_conn_params.timeout
                        );
            }
            break;

        case ESP_GAP_BLE_SCAN_RESULT_EVT:
            {
                const char * search_events[7] = {
                    "INQ_RES",
                    "INQ_CMPL",
                    "DISC_RES",
                    "DISC_BLE_RES",
                    "DISC_CMPL",
                    "DI_DISC_CMPL",
                    "SEARCH_CANCEL_CMPL"
                };

                const char * dev_types[]  = { "", "BREDR", "BLE", "DUMO" };
                const char * addr_types[] = { "PUBLIC", "RANDOM", "RPA_PUBLIC", "RPA_RANDOM" };

                NETWORK_BLUETOOTH_DEBUG_PRINTF(
                        "search_evt = %s",
                        search_events[param->scan_rst.search_evt]);

                if (param->scan_rst.dev_type <= 3 && param->scan_rst.ble_addr_type <= 3) {

                    NETWORK_BLUETOOTH_DEBUG_PRINTF(
                            ", dev_type = %s"
                            ", ble_addr_type = %s"
                            ", rssi = %d" ,
                            dev_types[param->scan_rst.dev_type],
                            addr_types[param->scan_rst.ble_addr_type],
                            param->scan_rst.rssi

                            );
                }

                if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {

                    NETWORK_BLUETOOTH_DEBUG_PRINTF(
                            ", bda = %02X:%02X:%02X:%02X:%02X:%02X"
                            ", adv_data_len = %u"
                            ", scan_rsp_len = %u" ,
                            param->scan_rst.bda[0],
                            param->scan_rst.bda[1],
                            param->scan_rst.bda[2],
                            param->scan_rst.bda[3],
                            param->scan_rst.bda[4],
                            param->scan_rst.bda[5],
                            param->scan_rst.adv_data_len,
                            param->scan_rst.scan_rsp_len);

                    uint8_t * adv_name;
                    uint8_t adv_name_len = 0;
                    adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv,
                            ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);

                    NETWORK_BLUETOOTH_DEBUG_PRINTF(", adv_name = ");
                    for (int j = 0; j < adv_name_len; j++) {
                        NETWORK_BLUETOOTH_DEBUG_PRINTF("%c", adv_name[j]);
                    }
                    NETWORK_BLUETOOTH_DEBUG_PRINTF("(%d bytes)", adv_name_len);
                }
            }
            break;

        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            break;
        case ESP_GAP_BLE_KEY_EVT:
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            break;
        case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
            break;
        case ESP_GAP_BLE_PASSKEY_REQ_EVT:
            break;
        case ESP_GAP_BLE_OOB_REQ_EVT:
            break;
        case ESP_GAP_BLE_LOCAL_IR_EVT:
            break;
        case ESP_GAP_BLE_LOCAL_ER_EVT:
            break;
        case ESP_GAP_BLE_NC_REQ_EVT:
            break;
    }

    NETWORK_BLUETOOTH_DEBUG_PRINTF(")\n");

}
#endif // EVENT_DEBUG

//--------------------------------
STATIC void btle_gap_event_handler
(
        esp_gap_ble_cb_event_t event,
        esp_ble_gap_cb_param_t *param) {

#ifdef EVENT_DEBUG_GAP
    gap_event_dump(event, param);
#endif

    btle_obj_t* bluetooth = btle_get_singleton();
    if (bluetooth->state != BTLE_STATE_INIT) {
        return;
    }

}

//---------------------------------------------------------------------------------------
STATIC void btle_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    btle_obj_t *self = MP_OBJ_TO_PTR(self_in);
    //#define BTLE_LF "\n"
#define BTLE_LF
    mp_printf(print,
            "Bluetooth(params=(conn_id=%04X" BTLE_LF
            ", gatts_connected=%s" BTLE_LF
            ", gatts_if=%u" BTLE_LF
            ", gattc_if=%u" BTLE_LF
            ", adv_params=("
            "adv_int_min=%u, " BTLE_LF
            "adv_int_max=%u, " BTLE_LF
            "adv_type=%u, " BTLE_LF
            "own_addr_type=%u, " BTLE_LF
            "peer_addr=%02X:%02X:%02X:%02X:%02X:%02X, " BTLE_LF
            "peer_addr_type=%u, " BTLE_LF
            "channel_map=%u, " BTLE_LF
            "adv_filter_policy=%u" BTLE_LF
            "state=%d" BTLE_LF
            ")"
            ,
            self->conn_id,
            self->gatts_connected ? "True" : "False",
            self->gatts_interface,
            self->gattc_interface,
            (unsigned int)(self->adv_params.adv_int_min / 1.6),
            (unsigned int)(self->adv_params.adv_int_max / 1.6),
            self->adv_params.adv_type,
            self->adv_params.own_addr_type,
            self->adv_params.peer_addr[0],
            self->adv_params.peer_addr[1],
            self->adv_params.peer_addr[2],
            self->adv_params.peer_addr[3],
            self->adv_params.peer_addr[4],
            self->adv_params.peer_addr[5],
            self->adv_params.peer_addr_type,
            self->adv_params.channel_map,
            self->adv_params.adv_filter_policy,
            self->state
                );
            mp_printf(print,
                    ", data=("
                    "set_scan_rsp=%s, " BTLE_LF
                    "include_name=%s, " BTLE_LF
                    "include_txpower=%s, " BTLE_LF
                    "min_interval=%d, " BTLE_LF
                    "max_interval=%d, " BTLE_LF
                    "appearance=%d, " BTLE_LF
                    "manufacturer_len=%d, " BTLE_LF
                    "p_manufacturer_data=%s, " BTLE_LF
                    "service_data_len=%d, " BTLE_LF
                    "p_service_data=",
                    self->adv_data.set_scan_rsp ? "True" : "False",
                    self->adv_data.include_name ? "True" : "False",
                    self->adv_data.include_txpower ? "True" : "False",
                    self->adv_data.min_interval,
                    self->adv_data.max_interval,
                    self->adv_data.appearance,
                    self->adv_data.manufacturer_len,
                    self->adv_data.p_manufacturer_data ? (const char *)self->adv_data.p_manufacturer_data : "nil",
                    self->adv_data.service_data_len);
            if (self->adv_data.p_service_data != NULL) {
                dumpBuf(self->adv_data.p_service_data, self->adv_data.service_data_len);
            }
            mp_printf(print, ", " BTLE_LF "flag=%d" BTLE_LF , self->adv_data.flag);
            mp_printf(print, ")");
}

// bluetooth.init(...)

//-----------------------------------------
STATIC mp_obj_t btle_init(mp_obj_t self_in)
{
    btle_obj_t * self = (btle_obj_t*)self_in;

    if (bluetooth_enabled) return mp_const_none;

    if (item_mut == NULL) {
        item_mut = xSemaphoreCreateMutex();
        xSemaphoreGive(item_mut);
    }

    if (callback_q == NULL) {
        callback_q = xQueueCreate(CALLBACK_QUEUE_SIZE, sizeof(callback_data_t));
        if (callback_q == NULL) {
            mp_raise_msg(&mp_type_OSError, "unable to create callback queue");
        }
    } else {
        xQueueReset(callback_q);
    }

    if (read_write_q == NULL) {
        read_write_q = xQueueCreate(1, sizeof(read_write_data_t));
        if (read_write_q == NULL) {
            mp_raise_msg(&mp_type_OSError, "unable to create read queue");
        }
    } else {
        xQueueReset(read_write_q);
    }

    if (self->state == BTLE_STATE_DEINIT) {

        self->state = BTLE_STATE_INIT;

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_bt_controller_init() failed");
        }

        // Enable Bluetooth dual mode
        if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_bt_controller_enable() failed");
        }

        if (esp_bluedroid_init() != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_bluedroid_init() failed");
        }

        if (esp_bluedroid_enable() != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_bluedroid_enable() failed");
        }


		#if CONFIG_GATTS_ENABLE
        if (esp_ble_gatts_register_callback(btle_gatts_event_handler) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_ble_gatts_register_callback() failed");
        }
		#endif

		#if CONFIG_GATTC_ENABLE
        if (esp_ble_gattc_register_callback(btle_gattc_event_handler) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_ble_gattc_register_callback() failed");
        }
		#endif

        if (esp_ble_gap_register_callback(btle_gap_event_handler) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_ble_gap_register_callback() failed");
        }

		#if CONFIG_GATTS_ENABLE
        if (esp_ble_gatts_app_register(0) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_ble_gatts_app_register() failed");
        }
		#endif

		#if CONFIG_GATTC_ENABLE
        if (esp_ble_gattc_app_register(1) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "esp_ble_gattc_app_register() failed");
        }
		#endif

    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(btle_init_obj, btle_init);

STATIC mp_obj_t btle_make_new(const mp_obj_type_t *type_in, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    printf("enter btle_make_new\n");
    btle_obj_t *self = btle_get_singleton();
    if (n_args != 0 || n_kw != 0) {
        mp_raise_TypeError("Constructor takes no arguments");
    }

    btle_init(self);
    return MP_OBJ_FROM_PTR(self);
}

STATIC void btle_adv_updated() {
    btle_obj_t* self = btle_get_singleton();
    esp_ble_gap_stop_advertising();
    esp_ble_gap_config_adv_data(&self->adv_data);

    // Restart will be handled in btle_gap_event_handler
}

STATIC mp_obj_t btle_adv_params(size_t n_args, const mp_obj_t *pos_args, mp_map_t * kw_args) {
    btle_obj_t *bluetooth = btle_get_singleton();

    if (bluetooth->state != BTLE_STATE_INIT) {
        mp_raise_msg(&mp_type_OSError, "bluetooth is deinit");
    }

    bool changed = false;

    enum {
        // params
        ARG_int_min,
        ARG_int_max,
        ARG_type,
        ARG_own_addr_type,
        ARG_peer_addr,
        ARG_peer_addr_type,
        ARG_channel_map,
        ARG_filter_policy
    };

    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_int_min,              MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_int_max,              MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_adv_type,             MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
        { MP_QSTR_own_addr_type,        MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
        { MP_QSTR_peer_addr,            MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = NULL }},
        { MP_QSTR_peer_addr_type,       MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
        { MP_QSTR_channel_map,          MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
        { MP_QSTR_filter_policy,        MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(0, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_buffer_info_t peer_addr_buf = { 0 };

    // pre-check complex types

    // peer_addr
    if (args[ARG_peer_addr].u_obj != NULL) {
        if (!MP_OBJ_IS_TYPE(args[ARG_peer_addr].u_obj, &mp_type_bytearray)) {
            BAD_PEER("peer addr ");
        }

        mp_get_buffer(args[ARG_peer_addr].u_obj, &peer_addr_buf, MP_BUFFER_READ);
        if (peer_addr_buf.len != ESP_BD_ADDR_LEN) {
            BAD_PEER("peer addr ");
        }
    }


    // update esp_ble_adv_params_t

    if (args[ARG_int_min].u_int != -1) {
        bluetooth->adv_params.adv_int_min = args[ARG_int_min].u_int * 1.6; // 0.625 msec per count
        changed = true;
    }

    if (args[ARG_int_max].u_int != -1) {
        bluetooth->adv_params.adv_int_max = args[ARG_int_max].u_int * 1.6;
        changed = true;
    }

    if (args[ARG_type].u_int != -1) {
        bluetooth->adv_params.adv_type = args[ARG_type].u_int;
        changed = true;
    }

    if (args[ARG_own_addr_type].u_int != -1) {
        bluetooth->adv_params.own_addr_type = args[ARG_own_addr_type].u_int;
        changed = true;
    }

    if (peer_addr_buf.buf != NULL) {
        memcpy(bluetooth->adv_params.peer_addr, peer_addr_buf.buf, ESP_BD_ADDR_LEN);
        changed = true;
    }

    if (args[ARG_peer_addr_type].u_int != -1) {
        bluetooth->adv_params.peer_addr_type = args[ARG_peer_addr_type].u_int;
        changed = true;
    }

    if (args[ARG_channel_map].u_int != -1) {
        bluetooth->adv_params.channel_map = args[ARG_channel_map].u_int;
        changed = true;
    }

    if (args[ARG_filter_policy].u_int != -1) {
        bluetooth->adv_params.adv_filter_policy = args[ARG_filter_policy].u_int;
        changed = true;
    }

    if (changed) {
        btle_adv_updated();
    }

    return mp_const_none;


}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(btle_adv_params_obj, 1, btle_adv_params);

STATIC mp_obj_t btle_adv_data(size_t n_args, const mp_obj_t *pos_args, mp_map_t * kw_args) {
    btle_obj_t *bluetooth = btle_get_singleton();

    if (bluetooth->state != BTLE_STATE_INIT) {
        mp_raise_msg(&mp_type_OSError, "bluetooth is deinit");
    }

    bool changed = false;
    bool unset_man_name = false;
    bool unset_dev_name = false;
    bool unset_uuid     = false;

    enum {
        // data
        ARG_is_scan_rsp,
        ARG_dev_name,
        ARG_man_name,
        ARG_inc_txpower,
        ARG_int_min,
        ARG_int_max,
        ARG_appearance,
        ARG_uuid,
        ARG_flags
    };

    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_is_scan_rsp,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_int = NULL}},
        { MP_QSTR_dev_name,         MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = NULL }},
        { MP_QSTR_man_name,         MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = NULL }},
        { MP_QSTR_inc_tx_power,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_int = NULL }},
        { MP_QSTR_int_min,          MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
        { MP_QSTR_int_max,          MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
        { MP_QSTR_appearance,       MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1 }},
        { MP_QSTR_uuid,             MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = NULL }},
        { MP_QSTR_flags,            MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = NULL }},
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(0, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_buffer_info_t man_name_buf = { 0 };
    mp_buffer_info_t dev_name_buf = { 0 };
    mp_buffer_info_t uuid_buf = { 0 };

    // pre-check complex types

    // man_name
    if (args[ARG_man_name].u_obj != NULL) {
        if (mp_obj_get_type(args[ARG_man_name].u_obj) == mp_const_none) {
            unset_man_name = true;
        } else if (!MP_OBJ_IS_STR_OR_BYTES(args[ARG_man_name].u_obj)) {
            mp_raise_ValueError("man_name must be type str or bytes");
        }
        mp_obj_str_get_buffer(args[ARG_man_name].u_obj, &man_name_buf, MP_BUFFER_READ);
    }

    // dev_name
    if (args[ARG_dev_name].u_obj != NULL) {
        if (mp_obj_get_type(args[ARG_dev_name].u_obj) == mp_const_none) {
            unset_dev_name = true;
        } else if (!MP_OBJ_IS_STR_OR_BYTES(args[ARG_dev_name].u_obj)) {
            mp_raise_ValueError("dev_name must be type str or bytes");
        }
        mp_obj_str_get_buffer(args[ARG_dev_name].u_obj, &dev_name_buf, MP_BUFFER_READ);
    }

    // uuid
    if (args[ARG_uuid].u_obj != NULL) {
        if (mp_obj_get_type(args[ARG_uuid].u_obj) == mp_const_none) {
            unset_uuid = true;
        } else if (!MP_OBJ_IS_TYPE(mp_obj_get_type(args[ARG_uuid].u_obj), &mp_type_bytearray)) {
            BAD_UUID("uuid ");
        }

        mp_get_buffer(args[ARG_uuid].u_obj, &uuid_buf, MP_BUFFER_READ);
        if (uuid_buf.len != UUID_LEN) {
            // FIXME: Is length really a fixed amount?
            BAD_UUID("uuid ");
        }
    }

    // update esp_ble_data_t

    if (args[ARG_is_scan_rsp].u_obj != NULL) {
        bluetooth->adv_data.set_scan_rsp = mp_obj_is_true(args[ARG_is_scan_rsp].u_obj);
        changed = true;
    }

    if (unset_dev_name) {
        esp_ble_gap_set_device_name("");
        bluetooth->adv_data.include_name = false;
    } else if (dev_name_buf.buf != NULL) {
        esp_ble_gap_set_device_name(dev_name_buf.buf);
        bluetooth->adv_data.include_name = dev_name_buf.len > 0;
        changed = true;
    }

    if (unset_man_name || man_name_buf.buf != NULL) {
        if (bluetooth->adv_data.p_manufacturer_data != NULL) {
            FREE(bluetooth->adv_data.p_manufacturer_data);
            bluetooth->adv_data.p_manufacturer_data = NULL;
        }

        bluetooth->adv_data.manufacturer_len = man_name_buf.len;
        if (man_name_buf.len > 0) {
            bluetooth->adv_data.p_manufacturer_data = MALLOC(man_name_buf.len);
            memcpy(bluetooth->adv_data.p_manufacturer_data, man_name_buf.buf, man_name_buf.len);
        }
        changed = true;
    }

    if (args[ARG_inc_txpower].u_obj != NULL) {
        bluetooth->adv_data.include_txpower = mp_obj_is_true(args[ARG_inc_txpower].u_obj);
        changed = true;
    }

    if (args[ARG_int_min].u_int != -1) {
        bluetooth->adv_data.min_interval = args[ARG_int_min].u_int;
        changed = true;
    }

    if (args[ARG_int_max].u_int != -1) {
        bluetooth->adv_data.max_interval = args[ARG_int_max].u_int;
        changed = true;
    }

    if (args[ARG_appearance].u_int != -1) {
        bluetooth->adv_data.appearance = args[ARG_appearance].u_int;
        changed = true;
    }

    if (unset_uuid || uuid_buf.buf != NULL) {

        if (bluetooth->adv_data.p_service_uuid != NULL) {
            FREE(bluetooth->adv_data.p_service_uuid);
            bluetooth->adv_data.p_service_uuid = NULL;
        }

        bluetooth->adv_data.service_uuid_len = uuid_buf.len;
        if (uuid_buf.len > 0) {
            bluetooth->adv_data.p_service_uuid = MALLOC(uuid_buf.len);
            bluetooth->adv_data.service_uuid_len = uuid_buf.len;
            memcpy(bluetooth->adv_data.p_service_uuid, uuid_buf.buf, uuid_buf.len);
        }
        changed = true;
    }

    if (args[ARG_flags].u_int != -1) {
        bluetooth->adv_data.flag = args[ARG_flags].u_int;
        changed = true;
    }

    if (changed) {
        btle_adv_updated();
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(btle_adv_data_obj, 1, btle_adv_data);

STATIC mp_obj_t btle_ble_adv_enable(mp_obj_t self_in, mp_obj_t enable_in) {
    btle_obj_t * self = MP_OBJ_TO_PTR(self_in);
    bool enable = mp_obj_is_true(enable_in);
    if (enable) {
        esp_ble_adv_params_t params = {
            .adv_int_min       = MSEC_0_625_TO_UNIT(100), // 100ms
            .adv_int_max       = MSEC_0_625_TO_UNIT(100), // 100ms
            .adv_type          = ADV_TYPE_IND,
            .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
            .channel_map       = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        esp_ble_gap_start_advertising(&params);
    } else {
        esp_ble_gap_stop_advertising();
    }
    self->advertising = enable;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(btle_ble_adv_enable_obj, btle_ble_adv_enable);


STATIC const mp_rom_map_elem_t btle_locals_dict_table[] = {
    // instance methods

    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&btle_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_adv_params), MP_ROM_PTR(&btle_adv_params_obj) },
    { MP_ROM_QSTR(MP_QSTR_adv_data), MP_ROM_PTR(&btle_adv_data_obj) },
    { MP_ROM_QSTR(MP_QSTR_ble_adv_enable), MP_ROM_PTR(&btle_ble_adv_enable_obj) },
    /*
       { MP_ROM_QSTR(MP_QSTR_ble_settings), MP_ROM_PTR(&btle_ble_settings_obj) },
       { MP_ROM_QSTR(MP_QSTR_ble_adv_enable), MP_ROM_PTR(&btle_ble_adv_enable_obj) },
       { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&btle_deinit_obj) },
       { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&btle_connect_obj) },
       { MP_ROM_QSTR(MP_QSTR_Service), MP_ROM_PTR(&btle_gatts_service_type) },
       { MP_ROM_QSTR(MP_QSTR_services), MP_ROM_PTR(&btle_services_obj) },
       { MP_ROM_QSTR(MP_QSTR_conns), MP_ROM_PTR(&btle_conns_obj) },
       { MP_ROM_QSTR(MP_QSTR_callback), MP_ROM_PTR(&btle_callback_obj) },
       { MP_ROM_QSTR(MP_QSTR_scan_start), MP_ROM_PTR(&btle_scan_start_obj) },
       { MP_ROM_QSTR(MP_QSTR_scan_stop), MP_ROM_PTR(&btle_scan_stop_obj) },
       { MP_ROM_QSTR(MP_QSTR_is_scanning), MP_ROM_PTR(&btle_is_scanning_obj) },

    // class constants

    // Callback types
    { MP_ROM_QSTR(MP_QSTR_CONNECT),                     MP_ROM_INT(BTLE_GATTS_CONNECT) },
    { MP_ROM_QSTR(MP_QSTR_DISCONNECT),                  MP_ROM_INT(BTLE_GATTS_DISCONNECT) },
    { MP_ROM_QSTR(MP_QSTR_READ),                        MP_ROM_INT(BTLE_READ) },
    { MP_ROM_QSTR(MP_QSTR_WRITE),                       MP_ROM_INT(BTLE_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_NOTIFY),                      MP_ROM_INT(BTLE_NOTIFY) },
    { MP_ROM_QSTR(MP_QSTR_SCAN_RES),                    MP_ROM_INT(BTLE_GATTC_SCAN_RES) },
    { MP_ROM_QSTR(MP_QSTR_SCAN_CMPL),                   MP_ROM_INT(BTLE_GATTC_SCAN_CMPL) },

    // esp_ble_adv_type_t
    { MP_ROM_QSTR(MP_QSTR_ADV_TYPE_IND),                MP_ROM_INT(ADV_TYPE_IND) },
    { MP_ROM_QSTR(MP_QSTR_ADV_TYPE_DIRECT_IND_HIGH),    MP_ROM_INT(ADV_TYPE_DIRECT_IND_HIGH) },
    { MP_ROM_QSTR(MP_QSTR_ADV_TYPE_SCAN_IND),           MP_ROM_INT(ADV_TYPE_SCAN_IND) },
    { MP_ROM_QSTR(MP_QSTR_ADV_TYPE_NONCONN_IND),        MP_ROM_INT(ADV_TYPE_NONCONN_IND) },
    { MP_ROM_QSTR(MP_QSTR_ADV_TYPE_DIRECT_IND_LOW),     MP_ROM_INT(ADV_TYPE_DIRECT_IND_LOW) },

    // esp_ble_addr_type_t
    { MP_ROM_QSTR(MP_QSTR_BLE_ADDR_TYPE_PUBLIC),        MP_ROM_INT(BLE_ADDR_TYPE_PUBLIC) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADDR_TYPE_RANDOM),        MP_ROM_INT(BLE_ADDR_TYPE_RANDOM) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADDR_TYPE_RPA_PUBLIC),    MP_ROM_INT(BLE_ADDR_TYPE_RPA_PUBLIC) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADDR_TYPE_RPA_RANDOM),    MP_ROM_INT(BLE_ADDR_TYPE_RPA_RANDOM) },

    // esp_ble_adv_channel_t
    { MP_ROM_QSTR(MP_QSTR_ADV_CHNL_37),                 MP_ROM_INT(ADV_CHNL_37) },
    { MP_ROM_QSTR(MP_QSTR_ADV_CHNL_38),                 MP_ROM_INT(ADV_CHNL_38) },
    { MP_ROM_QSTR(MP_QSTR_ADV_CHNL_39),                 MP_ROM_INT(ADV_CHNL_39) },
    { MP_ROM_QSTR(MP_QSTR_ADV_CHNL_ALL),                MP_ROM_INT(ADV_CHNL_ALL) },

    // BLE_ADV_DATA_FLAG

    { MP_ROM_QSTR(MP_QSTR_BLE_ADV_FLAG_LIMIT_DISC),         MP_ROM_INT(ESP_BLE_ADV_FLAG_LIMIT_DISC) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADV_FLAG_GEN_DISC),           MP_ROM_INT(ESP_BLE_ADV_FLAG_GEN_DISC) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADV_FLAG_BREDR_NOT_SPT),      MP_ROM_INT(ESP_BLE_ADV_FLAG_BREDR_NOT_SPT) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADV_FLAG_DMT_CONTROLLER_SPT), MP_ROM_INT(ESP_BLE_ADV_FLAG_DMT_CONTROLLER_SPT) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADV_FLAG_DMT_HOST_SPT),       MP_ROM_INT(ESP_BLE_ADV_FLAG_DMT_HOST_SPT) },
    { MP_ROM_QSTR(MP_QSTR_BLE_ADV_FLAG_NON_LIMIT_DISC),     MP_ROM_INT(ESP_BLE_ADV_FLAG_NON_LIMIT_DISC) },

    // Scan param constants
    { MP_ROM_QSTR(MP_QSTR_SCAN_TYPE_PASSIVE),               MP_ROM_INT(BLE_SCAN_TYPE_PASSIVE) },
    { MP_ROM_QSTR(MP_QSTR_SCAN_TYPE_ACTIVE),                MP_ROM_INT(BLE_SCAN_TYPE_ACTIVE) },

    { MP_ROM_QSTR(MP_QSTR_SCAN_FILTER_ALLOW_ALL),           MP_ROM_INT(BLE_SCAN_FILTER_ALLOW_ALL) },
    { MP_ROM_QSTR(MP_QSTR_SCAN_FILTER_ALLOW_ONLY_WLST),     MP_ROM_INT(BLE_SCAN_FILTER_ALLOW_ONLY_WLST) },
    { MP_ROM_QSTR(MP_QSTR_SCAN_FILTER_ALLOW_UND_RPA_DIR),   MP_ROM_INT(BLE_SCAN_FILTER_ALLOW_UND_RPA_DIR) },
    { MP_ROM_QSTR(MP_QSTR_SCAN_FILTER_ALLOW_WLIST_PRA_DIR), MP_ROM_INT(BLE_SCAN_FILTER_ALLOW_WLIST_PRA_DIR) },


    // exp_gatt_perm_t
    { MP_ROM_QSTR(MP_QSTR_PERM_READ),                   MP_ROM_INT(ESP_GATT_PERM_READ) },
    { MP_ROM_QSTR(MP_QSTR_PERM_READ_ENCRYPTED),         MP_ROM_INT(ESP_GATT_PERM_READ_ENCRYPTED) },
    { MP_ROM_QSTR(MP_QSTR_PERM_READ_ENC_MITM),          MP_ROM_INT(ESP_GATT_PERM_READ_ENC_MITM) },
    { MP_ROM_QSTR(MP_QSTR_PERM_WRITE),                  MP_ROM_INT(ESP_GATT_PERM_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_PERM_WRITE_ENCRYPTED),        MP_ROM_INT(ESP_GATT_PERM_WRITE_ENCRYPTED) },
    { MP_ROM_QSTR(MP_QSTR_PERM_WRITE_ENC_MITM),         MP_ROM_INT(ESP_GATT_PERM_WRITE_ENC_MITM) },
    { MP_ROM_QSTR(MP_QSTR_PERM_WRITE_SIGNED),           MP_ROM_INT(ESP_GATT_PERM_WRITE_SIGNED) },
    { MP_ROM_QSTR(MP_QSTR_PERM_WRITE_SIGNED_MITM),      MP_ROM_INT(ESP_GATT_PERM_WRITE_SIGNED_MITM) },

    // esp_gatt_char_prop_t

    { MP_ROM_QSTR(MP_QSTR_PROP_BROADCAST),              MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_BROADCAST) },
    { MP_ROM_QSTR(MP_QSTR_PROP_READ),                   MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_READ) },
    { MP_ROM_QSTR(MP_QSTR_PROP_WRITE_NR),               MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_WRITE_NR) },
    { MP_ROM_QSTR(MP_QSTR_PROP_WRITE),                  MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_NOTIFY),                 MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_NOTIFY) },
    { MP_ROM_QSTR(MP_QSTR_PROP_INDICATE),               MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_INDICATE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_AUTH),                   MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_AUTH) },
    { MP_ROM_QSTR(MP_QSTR_PROP_EXT_PROP),               MP_ROM_INT(ESP_GATT_CHAR_PROP_BIT_EXT_PROP) },

    // esp_ble_adv_filter_t
    {   MP_ROM_QSTR(MP_QSTR_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY),
        MP_ROM_INT(ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY)
    },
    {   MP_ROM_QSTR(MP_QSTR_ADV_FILTER_ALLOW_SCAN_WLST_CON_ANY),
        MP_ROM_INT(ADV_FILTER_ALLOW_SCAN_WLST_CON_ANY)
    },
    {   MP_ROM_QSTR(MP_QSTR_ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST),
        MP_ROM_INT(ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST)
    },
    {   MP_ROM_QSTR(MP_QSTR_ADV_FILTER_ALLOW_SCAN_WLST_CON_WLST),
        MP_ROM_INT(ADV_FILTER_ALLOW_SCAN_WLST_CON_WLST)
    },
    */
};

STATIC MP_DEFINE_CONST_DICT(btle_locals_dict, btle_locals_dict_table);

const mp_obj_type_t btle_type = {
    { &mp_type_type },
    .name = MP_QSTR_Bluetooth,
    .print = btle_print,
    .make_new = btle_make_new,
    .locals_dict = (mp_obj_dict_t*)&btle_locals_dict,
};


