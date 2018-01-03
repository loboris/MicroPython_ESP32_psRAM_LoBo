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
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"

#define ADC1_CHANNEL_HALL	ADC1_CHANNEL_MAX

typedef struct _madc_obj_t {
    mp_obj_base_t base;
    int gpio_id;
    adc1_channel_t adc1_id;
    adc_atten_t atten;
    adc_bits_width_t width;
} madc_obj_t;

static uint16_t adc_chan_used = 0;
static int8_t adc_width = -1;
static uint32_t adc_vref = 1100;


//----------------------------------------------------------------------------------------------------------
STATIC mp_obj_t madc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
	esp_err_t err = 0;

    mp_arg_check_num(n_args, n_kw, 1, 1, true);
    int pin_id = 0;

    if (MP_OBJ_IS_INT(args[0])) pin_id =  mp_obj_get_int(args[0]);
    else pin_id = machine_pin_get_id(args[0]);

    madc_obj_t *self = m_new_obj(madc_obj_t);;
    self->base.type = &machine_adc_type;

    if ((pin_id != ADC1_CHANNEL_HALL) && ((pin_id < 32) || (pin_id > 39))) mp_raise_ValueError("invalid Pin for ADC");

    self->atten = ADC_ATTEN_0db;
    self->width = ADC_WIDTH_BIT_12;

    if (pin_id != ADC1_CHANNEL_HALL) {
		if (pin_id > 35) self->adc1_id = ADC1_CHANNEL_0 + (pin_id-36);
		else self->adc1_id = ADC1_CHANNEL_4 + (pin_id-32);
        if ((adc_chan_used & 0x0100) && ((self->adc1_id == 36) || (self->adc1_id == 39))) mp_raise_ValueError("hall used, cannot use pins 36 & 39");
		adc_chan_used |= (1 << self->adc1_id);
	    self->gpio_id = pin_id;
	    self->gpio_id = pin_id;
	    err = adc1_config_channel_atten(self->adc1_id, ADC_ATTEN_0db);
	    if (err != ESP_OK) mp_raise_ValueError("Parameter Error");
    }
    else {
        if (adc_chan_used & 0x09) mp_raise_ValueError("adc on gpio 36 or 39 used");
    	self->adc1_id = ADC1_CHANNEL_HALL;
        self->gpio_id = GPIO_NUM_MAX;
    }

    if (adc_width != self->width) {
        err = adc1_config_width(self->width);
        if (err != ESP_OK) mp_raise_ValueError("Set width Error");
        adc_width = self->width;
    }

    return MP_OBJ_FROM_PTR(self);
}

//---------------------------------------------------------------------------------------
STATIC void madc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    madc_obj_t *self = self_in;
    char satten[16];
    char spin[8];
    if (self->atten == ADC_ATTEN_DB_0) sprintf(satten, "0dB (1.1V)");
    else if (self->atten == ADC_ATTEN_DB_2_5) sprintf(satten, "2.5dB (1.5V)");
    else if (self->atten == ADC_ATTEN_DB_6) sprintf(satten, "6dB (2.5V)");
    else if (self->atten == ADC_ATTEN_DB_11) sprintf(satten, "11dB (3.9V)");
    else sprintf(satten, "Unknown");

    if (self->gpio_id == GPIO_NUM_MAX) sprintf(spin, "HALL");
    else sprintf(spin, "Pin(%u)", self->gpio_id);

    mp_printf(print, "ADC(%s: width=%u bits, atten=%s, Vref=%u mV)", spin, self->width+9, satten, adc_vref);
}

//----------------------------------------------
STATIC mp_obj_t madc_readraw(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
    if (adc_width != self->width) {
        esp_err_t err = adc1_config_width(self->width);
        if (err != ESP_OK) mp_raise_ValueError("Set width Error");
        adc_width = self->width;
    }

    int val = 0;
    if (self->gpio_id == GPIO_NUM_MAX) val= hall_sensor_read();
    else val = adc1_get_raw(self->adc1_id);
    if (val == -1) mp_raise_ValueError("Parameter Error");

    return MP_OBJ_NEW_SMALL_INT(val);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_readraw_obj, madc_readraw);

//-------------------------------------------
STATIC mp_obj_t madc_read(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
    esp_adc_cal_characteristics_t characteristics;
    if (adc_width != self->width) {
        esp_err_t err = adc1_config_width(self->width);
        if (err != ESP_OK) mp_raise_ValueError("Set width Error");
    }

    int val = 0;
    if (self->gpio_id == GPIO_NUM_MAX) val= hall_sensor_read();
    else {
    	esp_adc_cal_get_characteristics(adc_vref, self->atten, self->width, &characteristics);
    	val = adc1_to_voltage(self->adc1_id, &characteristics);
    }
    return MP_OBJ_NEW_SMALL_INT(val);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_read_obj, madc_read);

//---------------------------------------------------------------
STATIC mp_obj_t madc_atten(mp_obj_t self_in, mp_obj_t atten_in) {
    madc_obj_t *self = self_in;
    if (self->gpio_id == GPIO_NUM_MAX) return mp_const_none;

    adc_atten_t atten = mp_obj_get_int(atten_in);
    if ((atten < ADC_ATTEN_DB_0) || (atten > ADC_ATTEN_DB_11)) mp_raise_ValueError("Unsupported atten value");

    esp_err_t err = adc1_config_channel_atten(self->adc1_id, atten);
    if (err != ESP_OK) mp_raise_ValueError("Parameter Error");
    self->atten = atten;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(madc_atten_obj, madc_atten);

//---------------------------------------------------------------
STATIC mp_obj_t madc_width(mp_obj_t self_in, mp_obj_t width_in) {
	madc_obj_t *self = self_in;
    if (self->gpio_id == GPIO_NUM_MAX) return mp_const_none;

	adc_bits_width_t width = mp_obj_get_int(width_in);
    if ((width < ADC_WIDTH_9Bit) || (width > ADC_WIDTH_12Bit)) mp_raise_ValueError("Unsupported width value");

    if (self) self->width = width;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(madc_width_obj, madc_width);


//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t madc_vref_togpio(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    const mp_arg_t allowed_args[] = {
			{ MP_QSTR_vref,			MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_vref_topin,	MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[0].u_int > 0) {
        uint32_t vref = args[0].u_int;
        if ((vref < 1000) || (vref > 1200)) mp_raise_ValueError("Vref range: 1000~1200 (mV)");
        adc_vref = vref;
    }
    uint32_t gpio = 0;
    if (args[1].u_int > 0) {
		gpio = args[1].u_int;
		if ((gpio < 25) || (gpio > 27)) mp_raise_ValueError("Only gpios 25,26,27 can be used");

		esp_err_t status = adc2_vref_to_gpio(gpio);
		if (status != ESP_OK) mp_raise_ValueError("Error routing Vref to gpio");

    }

    mp_obj_t tuple[2];
	tuple[0] = mp_obj_new_int(adc_vref);
	tuple[1] = mp_obj_new_int(gpio);
    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(madc_vref_togpio_obj, 0, madc_vref_togpio);


//=========================================================
STATIC const mp_rom_map_elem_t madc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read),		MP_ROM_PTR(&madc_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readraw),		MP_ROM_PTR(&madc_readraw_obj) },
    { MP_ROM_QSTR(MP_QSTR_atten),		MP_ROM_PTR(&madc_atten_obj) },
    { MP_ROM_QSTR(MP_QSTR_width),		MP_ROM_PTR(&madc_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_vref),		MP_ROM_PTR(&madc_vref_togpio_obj) },

    { MP_ROM_QSTR(MP_QSTR_HALL),		MP_ROM_INT(ADC1_CHANNEL_MAX) },

	{ MP_ROM_QSTR(MP_QSTR_ATTN_0DB),	MP_ROM_INT(ADC_ATTEN_0db) },
    { MP_ROM_QSTR(MP_QSTR_ATTN_2_5DB),	MP_ROM_INT(ADC_ATTEN_2_5db) },
    { MP_ROM_QSTR(MP_QSTR_ATTN_6DB),	MP_ROM_INT(ADC_ATTEN_6db) },
    { MP_ROM_QSTR(MP_QSTR_ATTN_11DB),	MP_ROM_INT(ADC_ATTEN_11db) },

    { MP_ROM_QSTR(MP_QSTR_WIDTH_9BIT),	MP_ROM_INT(ADC_WIDTH_9Bit) },
    { MP_ROM_QSTR(MP_QSTR_WIDTH_10BIT),	MP_ROM_INT(ADC_WIDTH_10Bit) },
    { MP_ROM_QSTR(MP_QSTR_WIDTH_11BIT),	MP_ROM_INT(ADC_WIDTH_11Bit) },
    { MP_ROM_QSTR(MP_QSTR_WIDTH_12BIT),	MP_ROM_INT(ADC_WIDTH_12Bit) },
};

STATIC MP_DEFINE_CONST_DICT(madc_locals_dict, madc_locals_dict_table);

//======================================
const mp_obj_type_t machine_adc_type = {
    { &mp_type_type },
    .name = MP_QSTR_ADC,
    .print = madc_print,
    .make_new = madc_make_new,
    .locals_dict = (mp_obj_t)&madc_locals_dict,
};
