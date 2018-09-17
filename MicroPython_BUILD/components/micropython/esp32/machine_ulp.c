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
#include "machine_ulp.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"

#include "ulp.h"
#include "esp_err.h"

typedef struct _machine_ulp_obj_t {
    mp_obj_base_t base;
} machine_ulp_obj_t;

const mp_obj_type_t machine_ulp_type;

// singleton ULP object
STATIC const machine_ulp_obj_t machine_ulp_obj = {{&machine_ulp_type}};

STATIC mp_obj_t machine_ulp_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    // return constant object
    return (mp_obj_t)&machine_ulp_obj;
}

STATIC mp_obj_t machine_ulp_set_wakeup_period(mp_obj_t self_in, mp_obj_t period_index_in, mp_obj_t period_us_in) {
    mp_uint_t period_index = mp_obj_get_int(period_index_in);
    mp_uint_t period_us = mp_obj_get_int(period_us_in);
    int _errno = ulp_set_wakeup_period(period_index, period_us);
    if (_errno != ESP_OK) {
	    mp_raise_OSError(_errno);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_ulp_set_wakeup_period_obj, machine_ulp_set_wakeup_period);

STATIC mp_obj_t machine_ulp_load_binary(mp_obj_t self_in, mp_obj_t load_addr_in, mp_obj_t program_binary_in) {
    mp_uint_t load_addr = mp_obj_get_int(load_addr_in);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(program_binary_in, &bufinfo, MP_BUFFER_READ);

    int _errno = ulp_load_binary(load_addr, bufinfo.buf, bufinfo.len/sizeof(uint32_t));
    if (_errno != ESP_OK) {
	    mp_raise_OSError(_errno);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_ulp_load_binary_obj, machine_ulp_load_binary);

STATIC mp_obj_t machine_ulp_run(mp_obj_t self_in, mp_obj_t entry_point_in) {
    mp_uint_t entry_point = mp_obj_get_int(entry_point_in);
    int _errno = ulp_run(entry_point/sizeof(uint32_t));
    if (_errno != ESP_OK) {
	    mp_raise_OSError(_errno);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ulp_run_obj, machine_ulp_run);

STATIC const mp_map_elem_t machine_ulp_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_wakeup_period), (mp_obj_t)&machine_ulp_set_wakeup_period_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_load_binary), (mp_obj_t)&machine_ulp_load_binary_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_run), (mp_obj_t)&machine_ulp_run_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_COPROC_RESERVE_MEM), MP_ROM_INT(CONFIG_ULP_COPROC_RESERVE_MEM) },
};
STATIC MP_DEFINE_CONST_DICT(machine_ulp_locals_dict, machine_ulp_locals_dict_table);

const mp_obj_type_t machine_ulp_type = {
    { &mp_type_type },
    .name = MP_QSTR_ULP,
    .make_new = machine_ulp_make_new,
    .locals_dict = (mp_obj_t)&machine_ulp_locals_dict,
};
