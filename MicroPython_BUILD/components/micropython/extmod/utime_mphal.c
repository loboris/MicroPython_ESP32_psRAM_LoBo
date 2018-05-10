/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2016 Damien P. George
 * Copyright (c) 2016 Paul Sokolovsky
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

#include "py/mpconfig.h"
#if MICROPY_PY_UTIME_MP_HAL

#include <string.h>

#include "py/obj.h"
#include "py/mphal.h"
#include "py/smallint.h"
#include "py/runtime.h"
#include "extmod/utime_mphal.h"

// Sleep for number of seconds (given as float)
//------------------------------------------------------------------
STATIC mp_obj_t time_sleep(mp_uint_t n_args, const mp_obj_t *args) {
    #if MICROPY_PY_BUILTINS_FLOAT
    uint32_t t = mp_hal_delay_ms((mp_uint_t)(1000 * mp_obj_get_float(args[0])));
    #else
    uint32_t t = mp_hal_delay_ms(1000 * mp_obj_get_int(args[0]));
    #endif
    if ((n_args > 1) && (mp_obj_is_true(args[1]))) {
		#if MICROPY_PY_BUILTINS_FLOAT
        return mp_obj_new_float((float)t / 1000.0);
		#else
    	return MP_OBJ_NEW_SMALL_INT(t);
		#endif
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_utime_sleep_obj, 1, 2, time_sleep);

// Sleep for number of milliseconds (given as integer)
// If the 2nd (optional) argument is set to True, return actual number of sleep ms
//---------------------------------------------------------------------
STATIC mp_obj_t time_sleep_ms(mp_uint_t n_args, const mp_obj_t *args) {
    mp_int_t ms = mp_obj_get_int(args[0]);
   	uint32_t t = mp_hal_delay_ms(ms);
    if ((n_args > 1) && (mp_obj_is_true(args[1]))) {
    	return MP_OBJ_NEW_SMALL_INT(t);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_utime_sleep_ms_obj, 1, 2, time_sleep_ms);

// Sleep for number of microseconds (given as integer)
//-------------------------------------------
STATIC mp_obj_t time_sleep_us(mp_obj_t arg) {
    mp_int_t us = mp_obj_get_int(arg);
    mp_hal_delay_us(us);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_utime_sleep_us_obj, time_sleep_us);

//-----------------------------------
STATIC mp_obj_t time_ticks_ms(void) {
	return mp_obj_new_int_from_ull(mp_hal_ticks_ms());
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_utime_ticks_ms_obj, time_ticks_ms);

//-----------------------------------
STATIC mp_obj_t time_ticks_us(void) {
    return mp_obj_new_int_from_ull(mp_hal_ticks_us());
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_utime_ticks_us_obj, time_ticks_us);

//------------------------------------
STATIC mp_obj_t time_ticks_cpu(void) {
    return MP_OBJ_NEW_SMALL_INT(mp_hal_ticks_cpu() & (MICROPY_PY_UTIME_TICKS_PERIOD - 1));
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_utime_ticks_cpu_obj, time_ticks_cpu);

//-------------------------------------------------------------------
STATIC mp_obj_t time_ticks_diff(mp_obj_t end_in, mp_obj_t start_in) {
    uint64_t start = mp_obj_get_int64(start_in);
    uint64_t end = mp_obj_get_int64(end_in);
    int64_t diff = end - start;
    return mp_obj_new_int_from_ll(diff);
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_utime_ticks_diff_obj, time_ticks_diff);

//----------------------------------------------------------------------
STATIC mp_obj_t time_tickscpu_diff(mp_obj_t end_in, mp_obj_t start_in) {
    // we assume that the arguments come from ticks_cpu so are small integers
    mp_uint_t start = MP_OBJ_SMALL_INT_VALUE(start_in);
    mp_uint_t end = MP_OBJ_SMALL_INT_VALUE(end_in);
    // Optimized formula avoiding if conditions. We adjust difference "forward",
    // wrap it around and adjust back.
    mp_int_t diff = ((end - start + MICROPY_PY_UTIME_TICKS_PERIOD / 2) & (MICROPY_PY_UTIME_TICKS_PERIOD - 1))
                   - MICROPY_PY_UTIME_TICKS_PERIOD / 2;
    return MP_OBJ_NEW_SMALL_INT(diff);
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_utime_tickscpu_diff_obj, time_tickscpu_diff);

//--------------------------------------------------------------------
STATIC mp_obj_t time_ticks_add(mp_obj_t ticks_in, mp_obj_t delta_in) {
    // both arguments can be 64-bit integers
    uint64_t tickin = mp_obj_get_int64(ticks_in);
    uint64_t delta = mp_obj_get_int64(delta_in);
    int64_t addtick = tickin + delta;
    return mp_obj_new_int_from_ll(addtick);
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_utime_ticks_add_obj, time_ticks_add);

#endif // MICROPY_PY_UTIME_MP_HAL
