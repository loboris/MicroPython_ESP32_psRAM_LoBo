/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2015 Daniel Campora
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
 * Copyright (c) 2017 Boris Lovosevic
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

#include <time.h>
#include <stdio.h>
#include <string.h>

#include "apps/sntp/sntp.h"

#include "py/nlr.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "esp_system.h"
#include "machine_rtc.h"
#include "soc/rtc.h"
#include "esp_clk.h"

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include "rom/uart.h"
#include "rom/crc.h"
#include "soc/soc.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "mphalport.h"

#define RTC_MEM_INT_SIZE 64
#define RTC_MEM_STR_SIZE 2048

#define MACHINE_RTC_VALID_EXT_PINS \
( \
    (1ll << 0)  | \
    (1ll << 2)  | \
    (1ll << 4)  | \
    (1ll << 12) | \
    (1ll << 13) | \
    (1ll << 14) | \
    (1ll << 15) | \
    (1ll << 25) | \
    (1ll << 26) | \
    (1ll << 27) | \
    (1ll << 32) | \
    (1ll << 33) | \
    (1ll << 34) | \
    (1ll << 35) | \
    (1ll << 36) | \
    (1ll << 37) | \
    (1ll << 38) | \
    (1ll << 39)   \
)

#define MACHINE_RTC_LAST_EXT_PIN 39

static int RTC_DATA_ATTR rtc_mem_int[RTC_MEM_INT_SIZE] = { 0 };
static char RTC_DATA_ATTR rtc_mem_str[RTC_MEM_STR_SIZE] = { 0 };
static uint16_t RTC_DATA_ATTR rtc_mem_int_crc;
static uint16_t RTC_DATA_ATTR rtc_mem_str_crc;

machine_rtc_config_t machine_rtc_config = { 0, -1, 0, 0, 0 };
uint32_t sntp_update_period = 3600000; // in ms


#define DEFAULT_SNTP_SERVER	"pool.ntp.org"

//------------------------------
typedef struct _mach_rtc_obj_t {
    mp_obj_base_t base;
    mp_obj_t sntp_server_name;
    bool   synced;
} mach_rtc_obj_t;

static RTC_DATA_ATTR uint64_t seconds_at_boot;

STATIC mach_rtc_obj_t mach_rtc_obj;
const mp_obj_type_t mach_rtc_type;

//------------------------------------------------------------
STATIC void mach_rtc_set_seconds_since_epoch(uint64_t nowus) {
    struct timeval tv;

    // store the packet timestamp
    gettimeofday(&tv, NULL);
    seconds_at_boot = tv.tv_sec;
}

//------------------------
static void rtc_init_mem()
{
    memset(rtc_mem_int, 0, sizeof(rtc_mem_int));
	memset(rtc_mem_str, 0, sizeof(rtc_mem_str));
	rtc_mem_int_crc = 0;
	rtc_mem_str_crc = 0;
}

//--------------------
void rtc_init0(void) {
    mach_rtc_set_seconds_since_epoch(0);
    rtc_init_mem();
}

//---------------------------
void mach_rtc_synced (void) {
    mach_rtc_obj.synced = true;
}

// Set system datetime
//-------------------------------------------------------
STATIC mp_obj_t mach_rtc_datetime(const mp_obj_t *args) {
    struct tm tm_info;

    // set date and time
    mp_obj_t *items;
    uint len;
    mp_obj_get_array(args[1], &len, &items);

    // verify the tuple
    if (len < 3 || len > 8) {
        mp_raise_ValueError("Invalid arguments");
    }

    tm_info.tm_year = mp_obj_get_int(items[0]) - 1900;
    tm_info.tm_mon = mp_obj_get_int(items[1]) - 1;
    tm_info.tm_mday = mp_obj_get_int(items[2]);
    if (len < 6) {
        tm_info.tm_sec = 0;
    } else {
        tm_info.tm_sec = mp_obj_get_int(items[5]);
    }
    if (len < 5) {
        tm_info.tm_min = 0;
    } else {
        tm_info.tm_min = mp_obj_get_int(items[4]);
    }
    if (len < 4) {
        tm_info.tm_hour = 0;
    } else {
        tm_info.tm_hour = mp_obj_get_int(items[3]);
    }

    int seconds = mktime(&tm_info);
    if (seconds == -1) seconds = 0;

    struct timeval now;
    now.tv_sec = seconds;
    now.tv_usec = 0;
    settimeofday(&now, NULL);

    mach_rtc_set_seconds_since_epoch(seconds);

    return mp_const_none;
}


//--------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mach_rtc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

     // setup the object
    mach_rtc_obj_t *self = &mach_rtc_obj;
    self->base.type = &mach_rtc_type;

    // return constant object
    return (mp_obj_t)&mach_rtc_obj;
}

//--------------------------------------------------------------
STATIC mp_obj_t mach_rtc_init(mp_obj_t self_in, mp_obj_t date) {
    mp_obj_t args[2] = {self_in, date};
    mach_rtc_datetime(args);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mach_rtc_init_obj, mach_rtc_init);

//-----------------------------------------------
STATIC mp_obj_t mach_rtc_now (mp_obj_t self_in) {

    // get the time from the RTC
    time_t now;
    time(&now);
    struct tm *tm_info;
    tm_info = localtime(&now);
    mp_obj_t tuple[8] = {
        mp_obj_new_int(tm_info->tm_year + 1900),
        mp_obj_new_int(tm_info->tm_mon + 1),
        mp_obj_new_int(tm_info->tm_mday),
        mp_obj_new_int(tm_info->tm_hour),
        mp_obj_new_int(tm_info->tm_min),
        mp_obj_new_int(tm_info->tm_sec),
        mp_obj_new_int(tm_info->tm_wday + 1),
        mp_obj_new_int(tm_info->tm_yday + 1)
    };

    return mp_obj_new_tuple(8, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mach_rtc_now_obj, mach_rtc_now);

//---------------------------------
void sntp_task (void *pvParameters)
{
    struct timeval tv;
    int synced = 0;

    while (!synced) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
		gettimeofday(&tv, NULL);
		// check if date > 2017/01/01
		if (tv.tv_sec > 1483228800) synced = true;
    }
    mach_rtc_obj.synced = true;
    mp_hal_ticks_base = tv.tv_sec;
    vTaskDelete(NULL);
}

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t mach_rtc_ntp_sync(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_server,           MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_update_period,                      MP_ARG_INT, {.u_int = 3600} },
    };

    mach_rtc_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    sntp_update_period = args[1].u_int * 1000;
    if (sntp_update_period < 60000) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "update period cannot be shorter than 60 s"));
    }

    mach_rtc_obj.synced = false;
    if (sntp_enabled()) {
        sntp_stop();
    }

    #ifdef MICROPY_TIMEZONE
    // ===== Set time zone ======
    setenv("TZ", MICROPY_TIMEZONE, 0);
    tzset();
    // ==========================
    #endif

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    self->sntp_server_name = args[0].u_obj;
    sntp_setservername(0, (char *)mp_obj_str_get_str(self->sntp_server_name));
    if (strlen(sntp_getservername(0)) == 0) {
        sntp_setservername(0, DEFAULT_SNTP_SERVER);
    }

    // set datetime to 1970/01/01 (epoch=0)
    // for 'synced' method to corectly detect synchronization
    struct timeval now;
    now.tv_sec = 0;
    now.tv_usec = 0;
    settimeofday(&now, NULL);
    mp_hal_ticks_base = 0;

    sntp_init();

    TaskHandle_t sntp_handle;
    xTaskCreate(&sntp_task, "SNTP", 512, NULL, CONFIG_MICROPY_TASK_PRIORITY+1, &sntp_handle);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mach_rtc_ntp_sync_obj, 1, mach_rtc_ntp_sync);

//------------------------------------------------------
STATIC mp_obj_t mach_rtc_has_synced (mp_obj_t self_in) {
    if (mach_rtc_obj.synced) {
        return mp_const_true;
    } else {
        return mp_const_false;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mach_rtc_has_synced_obj, mach_rtc_has_synced);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rtc_wake_on_ext0(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {ARG_pin, ARG_level};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin,  MP_ARG_OBJ, {.u_obj = mp_obj_new_int(machine_rtc_config.ext0_pin)} },
        { MP_QSTR_level,  MP_ARG_BOOL, {.u_bool = machine_rtc_config.ext0_level} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_pin].u_obj == mp_const_none) {
        machine_rtc_config.ext0_pin = -1; // "None"
    } else {
        gpio_num_t pin_id = machine_pin_get_id(args[ARG_pin].u_obj);
        if (pin_id != machine_rtc_config.ext0_pin) {
            if (!((1ll << pin_id) & MACHINE_RTC_VALID_EXT_PINS)) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "invalid pin"));
            }
            machine_rtc_config.ext0_pin = pin_id;
        }
    }

    machine_rtc_config.ext0_level = args[ARG_level].u_bool;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_rtc_wake_on_ext0_obj, 1, machine_rtc_wake_on_ext0);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rtc_wake_on_ext1(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {ARG_pins, ARG_level};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pins, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_level, MP_ARG_BOOL, {.u_bool = machine_rtc_config.ext1_level} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    uint64_t ext1_pins = machine_rtc_config.ext1_pins;


    // Check that all pins are allowed
    if (args[ARG_pins].u_obj != mp_const_none) {
        mp_uint_t len = 0;
        mp_obj_t *elem;
        mp_obj_get_array(args[ARG_pins].u_obj, &len, &elem);
        ext1_pins = 0;

        for (int i = 0; i < len; i++) {

            gpio_num_t pin_id = machine_pin_get_id(elem[i]);
            // mp_int_t pin = mp_obj_get_int(elem[i]);
            uint64_t pin_bit = (1ll << pin_id);

            if (!(pin_bit & MACHINE_RTC_VALID_EXT_PINS)) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "invalid pin"));
                break;
            }
            ext1_pins |= pin_bit;
        }
    }

    machine_rtc_config.ext1_level = args[ARG_level].u_bool;
    machine_rtc_config.ext1_pins = ext1_pins;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_rtc_wake_on_ext1_obj, 1, machine_rtc_wake_on_ext1);


// ====== RTC memory functions ============================

//--------------------------------------------------------------------------------
STATIC mp_obj_t esp_rtcmem_write(mp_obj_t self_in, mp_obj_t _pos, mp_obj_t _val) {
	int pos = mp_obj_get_int(_pos);
	int val = mp_obj_get_int(_val);

	if (pos >= RTC_MEM_INT_SIZE) {
		//mp_raise_msg(&mp_type_IndexError, "Index out of range");
		return mp_const_false;
	}
	rtc_mem_int[pos] = val;
	// Set CRC
	rtc_mem_int_crc = crc16_le(0, (uint8_t const *)rtc_mem_int, RTC_MEM_INT_SIZE*sizeof(int));

	return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(esp_rtcmem_write_obj, esp_rtcmem_write);

//----------------------------------------------------------------
STATIC mp_obj_t esp_rtcmem_read(mp_obj_t self_in, mp_obj_t _pos) {
	int pos = mp_obj_get_int(_pos);

	if (pos >= RTC_MEM_INT_SIZE) {
		//mp_raise_msg(&mp_type_IndexError, "Index out of range");
		return mp_const_none;
	}

	if (rtc_mem_int_crc != crc16_le(0, (uint8_t const *)rtc_mem_int, RTC_MEM_INT_SIZE*sizeof(int))) {
		return mp_const_none;
	}
	return mp_obj_new_int(rtc_mem_int[pos]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(esp_rtcmem_read_obj, esp_rtcmem_read);

//--------------------------------------------------------------------------
STATIC mp_obj_t esp_rtcmem_write_string(mp_obj_t self_in, mp_obj_t str_in) {
	const char *str = mp_obj_str_get_str(str_in);

	if (strlen(str) >= RTC_MEM_STR_SIZE) {
		//mp_raise_msg(&mp_type_ValueError, "String length too big");
		return mp_const_false;
	}
	memset(rtc_mem_str, 0, sizeof(rtc_mem_str));
	strcpy(rtc_mem_str, str);
	// Set CRC
	rtc_mem_str_crc = crc16_le(0, (uint8_t const *)rtc_mem_str, RTC_MEM_STR_SIZE);

	return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(esp_rtcmem_write_string_obj, esp_rtcmem_write_string);

//--------------------------------------------------------
STATIC mp_obj_t esp_rtcmem_read_string(mp_obj_t self_in) {

	if (rtc_mem_str_crc != crc16_le(0, (uint8_t const *)rtc_mem_str, RTC_MEM_STR_SIZE)) {
		return mp_const_none;
	}
	return mp_obj_new_str(rtc_mem_str, strlen(rtc_mem_str), false);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp_rtcmem_read_string_obj, esp_rtcmem_read_string);

//--------------------------------------------------
STATIC mp_obj_t esp_rtcmem_clear(mp_obj_t self_in) {

    rtc_init_mem();

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp_rtcmem_clear_obj, esp_rtcmem_clear);


//=========================================================
STATIC const mp_map_elem_t mach_rtc_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_init),			MP_ROM_PTR(&mach_rtc_init_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_now),				MP_ROM_PTR(&mach_rtc_now_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_ntp_sync),		MP_ROM_PTR(&mach_rtc_ntp_sync_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_synced),			MP_ROM_PTR(&mach_rtc_has_synced_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_wake_on_ext0),	MP_ROM_PTR(&machine_rtc_wake_on_ext0_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_wake_on_ext1),	MP_ROM_PTR(&machine_rtc_wake_on_ext1_obj) },

    {MP_OBJ_NEW_QSTR(MP_QSTR_write),			MP_ROM_PTR(&esp_rtcmem_write_obj)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_read),				MP_ROM_PTR(&esp_rtcmem_read_obj)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_clear),			MP_ROM_PTR(&esp_rtcmem_clear_obj)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_write_string),		MP_ROM_PTR(&esp_rtcmem_write_string_obj)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_read_string),		MP_ROM_PTR(&esp_rtcmem_read_string_obj)},
};
STATIC MP_DEFINE_CONST_DICT(mach_rtc_locals_dict, mach_rtc_locals_dict_table);

//===================================
const mp_obj_type_t mach_rtc_type = {
    { &mp_type_type },
    .name = MP_QSTR_RTC,
    .make_new = mach_rtc_make_new,
    .locals_dict = (mp_obj_t)&mach_rtc_locals_dict,
};
