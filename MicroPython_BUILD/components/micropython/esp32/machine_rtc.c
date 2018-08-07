/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 2014 Damien P. George
 * Copyright (c) 2015 Daniel Campora
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

#include <time.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "apps/sntp/sntp.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "rom/crc.h"

#include "py/nlr.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "machine_rtc.h"

#include "mphalport.h"
#include "modmachine.h"
#include "mpsleep.h"


#define RTC_MEM_INT_SIZE 64
#define RTC_MEM_STR_SIZE 2048

extern int MainTaskCore;

char mpy_time_zone[64] = {'\0'};

static int RTC_DATA_ATTR rtc_mem_int[RTC_MEM_INT_SIZE] = { 0 };
static char RTC_DATA_ATTR rtc_mem_str[RTC_MEM_STR_SIZE] = { 0 };
static uint16_t RTC_DATA_ATTR rtc_mem_int_crc;
static uint16_t RTC_DATA_ATTR rtc_mem_str_crc;

static TaskHandle_t sntp_handle = NULL;
xSemaphoreHandle sntp_mutex = NULL;

#define DEFAULT_SNTP_SERVER	"pool.ntp.org"

//------------------------------
typedef struct _mach_rtc_obj_t {
    mp_obj_base_t base;
    bool   synced;
    uint32_t sntp_update_period;
    char sntp_server_name[64];
} mach_rtc_obj_t;

static RTC_DATA_ATTR uint64_t seconds_at_boot;

static mach_rtc_obj_t mach_rtc_obj;
const mp_obj_type_t mach_rtc_type;


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
	mpsleep_reset_cause_t rstc = mpsleep_get_reset_cause();
	if ((rstc != MPSLEEP_DEEPSLEEP_RESET) && (rstc != MPSLEEP_SOFT_RESET) && (rstc != MPSLEEP_SOFT_CPU_RESET)) {
		seconds_at_boot = 0;
		setTicks_base(0);
	    rtc_init_mem();
	}
}

// Set system date time
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

	gettimeofday(&now, NULL);
	uint64_t ticks_us = ((((uint64_t)now.tv_sec * 1000000) + (uint64_t)now.tv_usec) - getTicks_base());

	now.tv_sec = seconds;
    now.tv_usec = 0;

	settimeofday(&now, NULL);
	// Set new base for ticks counting
    setTicks_base((((uint64_t)now.tv_sec * 1000000) - ticks_us));

	seconds_at_boot = seconds;

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

//------------------------------------
static void start_sntp(char *srv_name)
{
    if (sntp_enabled()) sntp_stop();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, srv_name);
    sntp_is_synced = false;
    sntp_init();
}

//---------------------------------
void sntp_task (void *pvParameters)
{
	mach_rtc_obj_t *rtc = (mach_rtc_obj_t *)pvParameters;
    struct timeval tv;
    uint32_t ellapsed=0, start_time;
    uint64_t ticks_us;
    int check_interval = 100;

    gettimeofday(&tv, NULL);
	start_time = tv.tv_sec;
	// get current ticks_us
	ticks_us = ((((uint64_t)tv.tv_sec * 1000000) + (uint64_t)tv.tv_usec) - getTicks_base());

	ESP_LOGD("SNTP_TASK", "start synchronization");
	start_sntp(rtc->sntp_server_name);

    while (1) {
        vTaskDelay(check_interval / portTICK_PERIOD_MS);
    	gettimeofday(&tv, NULL);
    	ticks_us += check_interval * 1000;
    	ellapsed += check_interval;

    	if (sntp_is_synced) {
			sntp_stop();
		    sntp_is_synced = false;
			ESP_LOGD("SNTP_TASK", "time synchronized");
			// Set new base for ticks counting
			setTicks_base((((uint64_t)tv.tv_sec * 1000000) + (uint64_t)tv.tv_usec - ticks_us));

			if (xSemaphoreTake(sntp_mutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
				rtc->synced = true;
				seconds_at_boot = tv.tv_sec;
				xSemaphoreGive(sntp_mutex);
			}
			// Terminate the task if periodic update is not requested
			if (rtc->sntp_update_period <= 10) break;
			// else prepare for next update
			ESP_LOGD("SNTP_TASK", "next update in %d seconds", rtc->sntp_update_period);
			start_time = tv.tv_sec;
			ticks_us = ((((uint64_t)tv.tv_sec * 1000000) + (uint64_t)tv.tv_usec) - getTicks_base());
			ellapsed = 0;
		    check_interval = 1000;
		}
		else {
			ellapsed = tv.tv_sec - start_time;
			if (ellapsed >= rtc->sntp_update_period) {
				// Update period expired, update time from server
			    check_interval = 100;
				start_time = tv.tv_sec;
				ESP_LOGD("SNTP_TASK", "start synchronization");
			    start_sntp(rtc->sntp_server_name);
			}
		}
    }
    // Terminate the task
    sntp_handle = NULL;
    vTaskDelete(NULL);
}

//--------------------------------------------
void tz_fromto_NVS(char *gettzs, char *settzs)
{
	size_t len = 0;
    char value[64] = {'\0'};
    if (gettzs) {
    	gettzs[0] = '\0';
        esp_err_t ret = nvs_get_str(mpy_nvs_handle, "MpyTimeZone", NULL, &len);
        if ((ret == ESP_OK ) && (len > 0) && (len < 64)) {
    		esp_err_t ret = nvs_get_str(mpy_nvs_handle, "MpyTimeZone", value, &len);
    		if ((ret == ESP_OK ) && (len > 0) && (len < 64)) {
    			if (gettzs) strcpy(gettzs, value);
    		}
        }
    }
	if (settzs) {
		esp_err_t esp_err = nvs_set_str(mpy_nvs_handle, "MpyTimeZone", settzs);
		if (ESP_OK == esp_err) {
			nvs_commit(mpy_nvs_handle);
		}
	}
}

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t mach_rtc_ntp_sync(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_server,           MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_update_period,                      MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_tz,                                 MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    mach_rtc_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);


    int period = args[1].u_int;
    if (period < 300) period = 10;

    char srv_name[64];
	sprintf(srv_name, "%s", DEFAULT_SNTP_SERVER);
	if (args[0].u_obj != mp_const_none) {
		const char *srvn = mp_obj_str_get_str(args[0].u_obj);
		if ((strlen(srvn) > 3) && (strlen(srvn) < 64)) sprintf(srv_name, "%s", srvn);
	}

	if (strlen(mpy_time_zone) == 0) {
		// Try to get tz from NVS
		tz_fromto_NVS(mpy_time_zone, NULL);
		if (strlen(mpy_time_zone) == 0) {
			#ifdef MICROPY_TIMEZONE
			// ===== Set default time zone ======
			snprintf(mpy_time_zone, sizeof(mpy_time_zone)-1, "%s", MICROPY_TIMEZONE);
			#endif
		}
	}

    if (args[2].u_obj != mp_const_none) {
    	// get TZ argument
    	const char *tzs = mp_obj_str_get_str(args[2].u_obj);
    	if ((strlen(tzs) > 2) && (strlen(tzs) < 64)) {
    	    sprintf(mpy_time_zone, "%s", tzs);
    		tz_fromto_NVS(NULL, mpy_time_zone);
    	}
    	else {
    		mp_raise_ValueError("tz string length must be 3 - 63");
    	}
    }
    setenv("TZ", mpy_time_zone, 1);
    tzset();

	if (sntp_mutex == NULL) {
		// Create sntp mutex
		sntp_mutex = xSemaphoreCreateMutex();
		if (sntp_mutex == NULL) {
	    	mp_raise_msg(&mp_type_OSError, "Error creating SNTP mutex");
		}
	}

	if (xSemaphoreTake(sntp_mutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
		sprintf(self->sntp_server_name, "%s", srv_name);
		self->sntp_update_period = period;
		self->synced = false;
		xSemaphoreGive(sntp_mutex);
	}
	else {
    	mp_raise_msg(&mp_type_OSError, "Error acquiring SNTP mutex");
	}

	if (sntp_handle == NULL) {
		// Create and start sntp task
		#if CONFIG_MICROPY_USE_BOTH_CORES
		int tres = xTaskCreate(&sntp_task, "SNTP_TASK", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &sntp_handle);
		#else
		int tres = xTaskCreatePinnedToCore(&sntp_task, "SNTP_TASK", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &sntp_handle, MainTaskCore);
		#endif
		if (tres != pdTRUE) {
	    	mp_raise_msg(&mp_type_OSError, "Error creating SNTP task");
		}
	}

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mach_rtc_ntp_sync_obj, 1, mach_rtc_ntp_sync);

//------------------------------------------------------
STATIC mp_obj_t mach_rtc_has_synced (mp_obj_t self_in) {
	if (sntp_mutex == NULL) return mp_const_false;

	mach_rtc_obj_t *self = MP_OBJ_TO_PTR(self_in);

	bool snc = false;
	if (xSemaphoreTake(sntp_mutex, 5000 / portTICK_PERIOD_MS) == pdTRUE) {
		snc = self->synced;
		xSemaphoreGive(sntp_mutex);
	}
	if (snc) return mp_const_true;
    else return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mach_rtc_has_synced_obj, mach_rtc_has_synced);

//------------------------------------------------------
STATIC mp_obj_t mach_rtc_sntp_state (mp_obj_t self_in) {
	if (sntp_mutex == NULL) return mp_const_false;

	mach_rtc_obj_t *self = MP_OBJ_TO_PTR(self_in);

	int period = 0;
	if (sntp_handle == NULL) return mp_const_false;

	if (xSemaphoreTake(sntp_mutex, 5000 / portTICK_PERIOD_MS) == pdTRUE) {
		period = self->sntp_update_period;
		xSemaphoreGive(sntp_mutex);
	}
	if (period == 0) return mp_const_false;
    else return mp_obj_new_int(period);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mach_rtc_sntp_state_obj, mach_rtc_sntp_state);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rtc_wake_on_ext0(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {ARG_pin, ARG_level, ARG_count};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin,		MP_ARG_OBJ,		{.u_obj = mp_obj_new_int(machine_rtc_config.ext0_pin)} },
        { MP_QSTR_level,	MP_ARG_BOOL,	{.u_bool = machine_rtc_config.ext0_level} },
        { MP_QSTR_count,	MP_ARG_INT,		{.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_pin].u_obj == mp_const_none) {
        machine_rtc_config.ext0_pin = -1; // "None"
    }
    else {
        int pin_id = machine_pin_get_gpio(args[ARG_pin].u_obj);
        if (pin_id != machine_rtc_config.ext0_pin) {
            if (!rtc_gpio_is_valid_gpio(pin_id)) {
            	mp_raise_ValueError("Invalid ext0 pin");
            }
            rtc_gpio_init(pin_id);
            rtc_gpio_set_direction(pin_id, RTC_GPIO_MODE_INPUT_ONLY);
            if (args[ARG_level].u_bool) {
            	rtc_gpio_pulldown_en(pin_id);
            	rtc_gpio_pullup_dis(pin_id);
            }
            else {
            	rtc_gpio_pulldown_dis(pin_id);
            	rtc_gpio_pullup_en(pin_id);
            }
            rtc_gpio_hold_en(pin_id);
            machine_rtc_config.ext0_pin = (int8_t)pin_id;
            machine_rtc_config.ext0_rtcpin = rtc_gpio_desc[pin_id].rtc_num;
        }
    }

    machine_rtc_config.ext0_level = args[ARG_level].u_bool;
    machine_rtc_config.ext0_count = args[ARG_count].u_int;
    machine_rtc_config.pulse_count = 0;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_rtc_wake_on_ext0_obj, 1, machine_rtc_wake_on_ext0);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_rtc_wake_on_ext1(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {ARG_pins, ARG_level};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pins,		MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_level,	MP_ARG_INT, {.u_int = machine_rtc_config.ext1_level} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint8_t ext1_pins[EXT1_WAKEUP_MAX_PINS] = {-1};
    uint32_t ext1_rtcpins[EXT1_WAKEUP_MAX_PINS] = {0};
    for (int i=0; i<EXT1_WAKEUP_MAX_PINS; i++) {
    	ext1_pins[i] = machine_rtc_config.ext1_pins[i];
    	ext1_rtcpins[i] = machine_rtc_config.ext1_rtcpins[i];
    }

    int ext1_level = args[ARG_level].u_int;
    if ((ext1_level < 0) || (ext1_level > 2)) {
        mp_raise_ValueError("Invalid ext1 level !");
    }

    // Check that all pins are allowed
    if (args[ARG_pins].u_obj != mp_const_none) {
        mp_uint_t len = 0;
        mp_obj_t *elem;
        mp_obj_get_array(args[ARG_pins].u_obj, &len, &elem);
        int pins = (len > EXT1_WAKEUP_MAX_PINS) ? EXT1_WAKEUP_MAX_PINS : len;

        for (int i = 0; i < pins; i++) {
            int pin_id = machine_pin_get_gpio(elem[i]);

            if (!rtc_gpio_is_valid_gpio(pin_id)) {
            	mp_raise_ValueError("Invalid ext1 pin");
                break;
            }
            rtc_gpio_init(pin_id);
            rtc_gpio_set_direction(pin_id, RTC_GPIO_MODE_INPUT_ONLY);
            if (args[ARG_level].u_bool) {
            	rtc_gpio_pulldown_en(pin_id);
            	rtc_gpio_pullup_dis(pin_id);
            }
            else {
            	rtc_gpio_pulldown_dis(pin_id);
            	rtc_gpio_pullup_en(pin_id);
            }
            rtc_gpio_hold_en(pin_id);
        	ext1_pins[i] = pin_id;
        	ext1_rtcpins[i] = rtc_gpio_desc[pin_id].rtc_num;
        }
    }
    else {
        for (int i=0; i<EXT1_WAKEUP_MAX_PINS; i++) {
        	ext1_pins[i] = -1;
        	ext1_rtcpins[i] = 0;
        }
    }

    machine_rtc_config.ext1_level = (uint8_t)ext1_level;
    for (int i=0; i<EXT1_WAKEUP_MAX_PINS; i++) {
    	machine_rtc_config.ext1_pins[i] = ext1_pins[i];
    	machine_rtc_config.ext1_rtcpins[i] = ext1_rtcpins[i];
    }

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
	return mp_obj_new_str(rtc_mem_str, strlen(rtc_mem_str));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp_rtcmem_read_string_obj, esp_rtcmem_read_string);

//--------------------------------------------------
STATIC mp_obj_t esp_rtcmem_clear(mp_obj_t self_in) {

    rtc_init_mem();

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp_rtcmem_clear_obj, esp_rtcmem_clear);

//--------------------------------------------------------------------------------------------
STATIC void machine_rtc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
	char ext0[64] = {'\0'};
	char ext1[32 + (EXT1_WAKEUP_MAX_PINS*3)] = {'\0'};

	if (machine_rtc_config.ext0_pin >= 0) {
		sprintf(ext0, "Wake on EXT0: Pin=%d, Level=%s, Count=%d",
				machine_rtc_config.ext0_pin, machine_rtc_config.ext0_level ? "High" : "Low", machine_rtc_config.ext0_count);
	}
	int has_ext1_pins = 0;
    for (int i=0; i<EXT1_WAKEUP_MAX_PINS; i++) {
    	if (machine_rtc_config.ext1_pins[i] >= 0) has_ext1_pins++;
    }

	if (has_ext1_pins) {
		if (strlen(ext0) > 0) strcat(ext0, ";  ");
		sprintf(ext1, "Wake on EXT1: Pins (");
		char stemp[16];
		for (int i=0; i<EXT1_WAKEUP_MAX_PINS; i++) {
            if (machine_rtc_config.ext1_pins[i] >= 0) {
            	sprintf(stemp, "%d,", machine_rtc_config.ext1_pins[i]);
            	strcat(ext1, stemp);
            }
		}
		if (ext1[strlen(ext1)-1] == ',') ext1[strlen(ext1)-1] = '\0';
		strcat(ext1, ")");

		stemp[0] = '\0';
		if (machine_rtc_config.ext1_level == ESP_EXT1_WAKEUP_ANY_HIGH) sprintf(stemp, "Any High");
		else if (machine_rtc_config.ext1_level == ESP_EXT1_WAKEUP_ALL_LOW) sprintf(stemp, "All Low");
		else if (machine_rtc_config.ext1_level == EXT1_WAKEUP_ALL_HIGH) sprintf(stemp, "All High");
		if (strlen(stemp) > 0) {
			strcat(ext1, ", Level: ");
			strcat(ext1, stemp);
		}
	}
	mp_printf(print, "RTC (");
	if (strlen(ext0) > 0) mp_printf(print, " %s", ext0);
	if (strlen(ext1) > 0) mp_printf(print, "%s", ext1);
	mp_printf(print, " )");
}


//=========================================================
STATIC const mp_map_elem_t mach_rtc_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_init),			(mp_obj_t)&mach_rtc_init_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_now),				(mp_obj_t)&mach_rtc_now_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_ntp_sync),		(mp_obj_t)&mach_rtc_ntp_sync_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_ntp_state),		(mp_obj_t)&mach_rtc_sntp_state_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_synced),			(mp_obj_t)&mach_rtc_has_synced_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_wake_on_ext0),	(mp_obj_t)&machine_rtc_wake_on_ext0_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_wake_on_ext1),	(mp_obj_t)&machine_rtc_wake_on_ext1_obj },

    { MP_OBJ_NEW_QSTR(MP_QSTR_write),			(mp_obj_t)&esp_rtcmem_write_obj},
    { MP_OBJ_NEW_QSTR(MP_QSTR_read),			(mp_obj_t)&esp_rtcmem_read_obj},
    { MP_OBJ_NEW_QSTR(MP_QSTR_clear),			(mp_obj_t)&esp_rtcmem_clear_obj},
    { MP_OBJ_NEW_QSTR(MP_QSTR_write_string),	(mp_obj_t)&esp_rtcmem_write_string_obj},
    { MP_OBJ_NEW_QSTR(MP_QSTR_read_string),		(mp_obj_t)&esp_rtcmem_read_string_obj},

	// Constants
	{ MP_ROM_QSTR(MP_QSTR_EXT1_ANYHIGH),		MP_ROM_INT(ESP_EXT1_WAKEUP_ANY_HIGH) },
	{ MP_ROM_QSTR(MP_QSTR_EXT1_ALLLOW),			MP_ROM_INT(ESP_EXT1_WAKEUP_ALL_LOW) },
	{ MP_ROM_QSTR(MP_QSTR_EXT1_ALLHIGH),		MP_ROM_INT(EXT1_WAKEUP_ALL_HIGH) },
};
STATIC MP_DEFINE_CONST_DICT(mach_rtc_locals_dict, mach_rtc_locals_dict_table);

//===================================
const mp_obj_type_t mach_rtc_type = {
    { &mp_type_type },
    .name = MP_QSTR_RTC,
	.print = machine_rtc_print,
    .make_new = mach_rtc_make_new,
    .locals_dict = (mp_obj_t)&mach_rtc_locals_dict,
};
