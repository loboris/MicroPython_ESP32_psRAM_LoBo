/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
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

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "extmod/utime_mphal.h"
#include "py/runtime.h"
#include "mphalport.h"
#include "machine_rtc.h"

//-------------------------------
STATIC mp_obj_t time_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    mp_float_t curtime = (double)tv.tv_sec + (double)((double)tv.tv_usec / 1000000.0);
    return mp_obj_new_float(curtime);
}
MP_DEFINE_CONST_FUN_OBJ_0(time_time_obj, time_time);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t time_localtime(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    const mp_arg_t allowed_args[] = {
		{ MP_QSTR_secs, MP_ARG_INT, { .u_int = -1 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    time_t seconds;
    struct tm *tm_info;

    if (args[0].u_int >= 0) seconds = args[0].u_int;
    else time(&seconds); // get the time from the RTC

    tm_info = localtime(&seconds);
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
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(time_localtime_obj, 0, time_localtime);

//------------------------------------------------------------------------------------------
STATIC mp_obj_t time_gmtime(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    const mp_arg_t allowed_args[] = {
		{ MP_QSTR_secs, MP_ARG_INT, { .u_int = -1 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    time_t seconds;
    struct tm *tm_info;

    if (args[0].u_int >= 0) seconds = args[0].u_int;
    else time(&seconds); // get the time from the RTC

    tm_info = gmtime(&seconds);
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
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(time_gmtime_obj, 0, time_gmtime);

//------------------------------------------------------------------------------------------
STATIC mp_obj_t time_strftime(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    const mp_arg_t allowed_args[] = {
		{ MP_QSTR_format, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
		{ MP_QSTR_time,                     MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *fmt = (char *)mp_obj_str_get_str(args[0].u_obj);
    char str_time[128];
    struct tm *tm_info;
    struct tm tm_inf;

    if (args[1].u_obj != mp_const_none) {
        mp_obj_t *time_items;

        mp_obj_get_array_fixed_n(args[1].u_obj, 8, &time_items);

		tm_inf.tm_year = mp_obj_get_int(time_items[0]) - 1900;
        tm_inf.tm_mon = mp_obj_get_int(time_items[1]) - 1;
        tm_inf.tm_mday = mp_obj_get_int(time_items[2]);
        tm_inf.tm_hour = mp_obj_get_int(time_items[3]);
        tm_inf.tm_min = mp_obj_get_int(time_items[4]);
        tm_inf.tm_sec = mp_obj_get_int(time_items[5]);
        tm_inf.tm_wday = mp_obj_get_int(time_items[6]) - 1;
        tm_inf.tm_yday = mp_obj_get_int(time_items[7]) - 1;
        tm_info = &tm_inf;
    }
    else {
        time_t seconds;
        time(&seconds); // get the time from the RTC
        tm_info = gmtime(&seconds);
    }

    strftime(str_time, 127, fmt, tm_info);

    return mp_obj_new_str(str_time, strlen(str_time));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(time_strftime_obj, 1, time_strftime);

//------------------------------------------------------------------------------------------
STATIC mp_obj_t time_mktime(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    const mp_arg_t allowed_args[] = {
		{ MP_QSTR_time, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    struct tm tm_inf;
	mp_obj_t *time_items;

	mp_obj_get_array_fixed_n(args[0].u_obj, 8, &time_items);

	tm_inf.tm_year = mp_obj_get_int(time_items[0]) - 1900;
	tm_inf.tm_mon = mp_obj_get_int(time_items[1]) - 1;
	tm_inf.tm_mday = mp_obj_get_int(time_items[2]);
	tm_inf.tm_hour = mp_obj_get_int(time_items[3]);
	tm_inf.tm_min = mp_obj_get_int(time_items[4]);
	tm_inf.tm_sec = mp_obj_get_int(time_items[5]);
	tm_inf.tm_wday = mp_obj_get_int(time_items[6]) - 1;
	tm_inf.tm_yday = mp_obj_get_int(time_items[7]) - 1;

    time_t seconds = mktime(&tm_inf);

    return mp_obj_new_float((double)seconds);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(time_mktime_obj, 1, time_mktime);

//-------------------------------------
STATIC mp_obj_t time_ticks_base(void) {
    return mp_obj_new_int_from_ull(getTicks_base());
}
MP_DEFINE_CONST_FUN_OBJ_0(time_ticks_base_obj, time_ticks_base);

//---------------------------------------------------
STATIC mp_obj_t time_block_sleep(mp_obj_t tm_sleep) {
    #if MICROPY_PY_BUILTINS_FLOAT
    vTaskDelay((uint32_t)(1000 * mp_obj_get_float(tm_sleep)) / portTICK_RATE_MS);
    #else
    uint32_t t = mp_hal_delay_ms(1000 * mp_obj_get_int(args[0]));
    vTaskDelay((uint32_t)(1000 * mp_obj_get_int(tm_sleep)) / portTICK_RATE_MS);
    #endif
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_utime_block_sleep_obj, time_block_sleep);


//============================================================
STATIC const mp_rom_map_elem_t time_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_utime) },

    { MP_ROM_QSTR(MP_QSTR_time),           MP_ROM_PTR(&time_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_mktime),         MP_ROM_PTR(&time_mktime_obj) },
    { MP_ROM_QSTR(MP_QSTR_localtime),      MP_ROM_PTR(&time_localtime_obj) },
    { MP_ROM_QSTR(MP_QSTR_gmtime),         MP_ROM_PTR(&time_gmtime_obj) },
    { MP_ROM_QSTR(MP_QSTR_strftime),       MP_ROM_PTR(&time_strftime_obj) },

	{ MP_ROM_QSTR(MP_QSTR_sleep),          MP_ROM_PTR(&mp_utime_sleep_obj) },
	{ MP_ROM_QSTR(MP_QSTR_block_sleep),    MP_ROM_PTR(&mp_utime_block_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms),       MP_ROM_PTR(&mp_utime_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_us),       MP_ROM_PTR(&mp_utime_sleep_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms),       MP_ROM_PTR(&mp_utime_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_us),       MP_ROM_PTR(&mp_utime_ticks_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_cpu),      MP_ROM_PTR(&mp_utime_ticks_cpu_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_add),      MP_ROM_PTR(&mp_utime_ticks_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_diff),     MP_ROM_PTR(&mp_utime_ticks_diff_obj) },
    { MP_ROM_QSTR(MP_QSTR_tickscpu_diff),  MP_ROM_PTR(&mp_utime_tickscpu_diff_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_base),     MP_ROM_PTR(&time_ticks_base_obj) },
};
STATIC MP_DEFINE_CONST_DICT(time_module_globals, time_module_globals_table);

//====================================
const mp_obj_module_t utime_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&time_module_globals,
};
