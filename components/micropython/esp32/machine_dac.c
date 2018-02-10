/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
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

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/dac.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"

typedef struct _mdac_obj_t {
    mp_obj_base_t base;
    gpio_num_t gpio_id;
    dac_channel_t dac_id;
} mdac_obj_t;

STATIC const mdac_obj_t mdac_obj[] = {
    {{&machine_dac_type}, GPIO_NUM_25, DAC_CHANNEL_1},
    {{&machine_dac_type}, GPIO_NUM_26, DAC_CHANNEL_2},
};

STATIC mp_obj_t mdac_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw,
        const mp_obj_t *args) {

    mp_arg_check_num(n_args, n_kw, 1, 1, true);
    gpio_num_t pin_id = machine_pin_get_id(args[0]);
    const mdac_obj_t *self = NULL;
    for (int i = 0; i < MP_ARRAY_SIZE(mdac_obj); i++) {
        if (pin_id == mdac_obj[i].gpio_id) { self = &mdac_obj[i]; break; }
    }
    if (!self) mp_raise_ValueError("invalid Pin for DAC");

    esp_err_t err = dac_output_enable(self->dac_id);
    if (err == ESP_OK) {
        err = dac_output_voltage(self->dac_id, 0);
    }
    if (err == ESP_OK) return MP_OBJ_FROM_PTR(self);
    mp_raise_ValueError("Parameter Error");
}

STATIC void mdac_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mdac_obj_t *self = self_in;
    mp_printf(print, "DAC(Pin(%u))", self->gpio_id);
}

STATIC mp_obj_t mdac_write(mp_obj_t self_in, mp_obj_t value_in) {
    mdac_obj_t *self = self_in;
    int value = mp_obj_get_int(value_in);
    if (value < 0 || value > 255) mp_raise_ValueError("Value out of range");

    esp_err_t err = dac_output_voltage(self->dac_id, value);
    if (err == ESP_OK) return mp_const_none;
    mp_raise_ValueError("Parameter Error");
}
MP_DEFINE_CONST_FUN_OBJ_2(mdac_write_obj, mdac_write);

STATIC const mp_rom_map_elem_t mdac_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mdac_write_obj) },
};

STATIC MP_DEFINE_CONST_DICT(mdac_locals_dict, mdac_locals_dict_table);

const mp_obj_type_t machine_dac_type = {
    { &mp_type_type },
    .name = MP_QSTR_DAC,
    .print = mdac_print,
    .make_new = mdac_make_new,
    .locals_dict = (mp_obj_t)&mdac_locals_dict,
};
