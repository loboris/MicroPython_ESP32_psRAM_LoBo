/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
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

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"
#include "machine_pin.h"

#define ADC1_CHANNEL_HALL	ADC1_CHANNEL_MAX

typedef struct _madc_obj_t {
    mp_obj_base_t base;
    int gpio_id;
    adc_unit_t adc_num;
    adc1_channel_t adc_chan;
    adc_atten_t atten;
    adc_bits_width_t width;
} madc_obj_t;

static uint16_t adc1_chan_used = 0;
static uint16_t adc2_chan_used = 0;
static int8_t adc_width = -1;
static int8_t last_adc_width = -1;
static int8_t last_adc_num = -1;
static uint32_t adc_vref = 1100;
static uint32_t last_adc_vref = 0;
static adc_atten_t last_atten = ADC_ATTEN_MAX;
static adc_atten_t last_atten2 = ADC_ATTEN_MAX;
static esp_adc_cal_characteristics_t characteristics;

static const uint8_t adc1_gpios[ADC1_CHANNEL_MAX] = {36, 37, 38, 39, 32, 33, 34, 35};
static const uint8_t adc2_gpios[ADC2_CHANNEL_MAX] = {4, 0, 2, 15, 13, 12, 14, 27, 25, 26};

//-------------------------------------
static void set_width(madc_obj_t *self)
{
    if (adc_width != self->width) {
    	if (self->adc_num == ADC_UNIT_1) {
			esp_err_t err = adc1_config_width(self->width);
			if (err != ESP_OK) mp_raise_ValueError("Set width Error");
    	}
    	else {
    		adc_set_data_width(self->adc_num, self->width);
    	}
		adc_width = self->width;
    }
}

//-----------------------------------------------------
static int get_adc_channel(adc_unit_t adc_num, int pin)
{
	int channel = -1;
	if (adc_num == ADC_UNIT_1) {
		for (int i=0; i < ADC1_CHANNEL_MAX; i++) {
			if (adc1_gpios[i] == pin) {
				channel = i;
				break;
			}
		}
	}
	else {
		for (int i=0; i < ADC2_CHANNEL_MAX; i++) {
			if (adc2_gpios[i] == pin) {
				channel = i;
				break;
			}
		}
	}
	return channel;
}

//------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t madc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_pin, ARG_unit };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin,	MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none}},
        { MP_QSTR_unit,	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = ADC_UNIT_1}},
    };
    // parse arguments
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

	esp_err_t err = 0;

    mp_arg_check_num(n_args, n_kw, 1, 1, true);
    int pin_id = 0;

    pin_id = machine_pin_get_gpio(args[ARG_pin].u_obj);

    madc_obj_t *self = m_new_obj(madc_obj_t);;
    self->base.type = &machine_adc_type;

    self->adc_num = args[ARG_unit].u_int;
    if ((self->adc_num != ADC_UNIT_1) && (self->adc_num != ADC_UNIT_2)) {
    	mp_raise_ValueError("invalid ADC unit (1 and 2 allowed)");
    }
    self->atten = ADC_ATTEN_DB_0;
    self->width = ADC_WIDTH_BIT_12;

	if (pin_id != ADC1_CHANNEL_HALL) {
		int channel = get_adc_channel(self->adc_num, pin_id);
		if (channel < 0) mp_raise_ValueError("invalid Pin for ADC");
		self->adc_chan = channel;
		self->gpio_id = pin_id;

		if (self->adc_num == ADC_UNIT_1) {
			if ((adc1_chan_used & 0x0100) && ((pin_id == 36) || (pin_id == 39))) mp_raise_ValueError("hall used, cannot use pins 36 & 39");
			if (adc1_chan_used & (1 << self->adc_chan)) mp_raise_ValueError("pin already used for adc");
			adc1_chan_used |= (1 << self->adc_chan);

			err = adc_gpio_init(self->adc_num, self->adc_chan);
			if (err != ESP_OK) mp_raise_ValueError("Error configuring ADC gpio");
			err = adc1_config_channel_atten(self->adc_chan, ADC_ATTEN_DB_0);
			if (err != ESP_OK) mp_raise_ValueError("Error configuring attenuation");
    	}
    	else {
			if (adc2_chan_used & (1 << self->adc_chan)) mp_raise_ValueError("pin already used for adc");
			adc2_chan_used |= (1 << self->adc_chan);

		    gpio_pad_select_gpio(self->gpio_id);
		    gpio_set_direction(self->gpio_id, GPIO_MODE_DISABLE);
		    gpio_set_pull_mode(self->gpio_id, GPIO_FLOATING);

			adc_gpio_init(self->adc_num, self->adc_chan);
			if (err != ESP_OK) mp_raise_ValueError("Error configuring ADC gpio");
			if (last_atten2 != self->atten) {
				adc2_config_channel_atten(self->adc_chan, self->atten);
				last_atten2 = self->atten;
			}
    	}
    }
    else {
    	self->adc_num = ADC_UNIT_1;
        if (adc1_chan_used & 0x09) mp_raise_ValueError("adc on gpio 36 or 39 used");
        if (adc1_chan_used & 0x0100) mp_raise_ValueError("hall already used");
        adc1_chan_used |= 0x0100;
    	self->adc_chan = ADC1_CHANNEL_HALL;
        self->gpio_id = GPIO_NUM_MAX;
    }

    set_width(self);

    return MP_OBJ_FROM_PTR(self);
}

//-------------------------------------------
STATIC mp_obj_t madc_deinit(mp_obj_t self_in)
{
    madc_obj_t *self = self_in;
	if (self->gpio_id < 0) return mp_const_none;

	if (self->adc_num == ADC_UNIT_1) {
		if (self->adc_chan == ADC1_CHANNEL_HALL) {
			adc1_chan_used &= 0x00FF;
			gpio_pad_select_gpio(36);
			gpio_pad_select_gpio(39);
		}
		else {
			adc1_chan_used &= (~(1 << self->adc_chan) & 0x1FF);
			gpio_pad_select_gpio(self->gpio_id);
		    gpio_set_direction(self->gpio_id, GPIO_MODE_INPUT);
		    gpio_set_pull_mode(self->gpio_id, GPIO_FLOATING);
		}
	}
	else {
		adc2_chan_used &= (~(1 << self->adc_chan) & 0x3FF);
		gpio_pad_select_gpio(self->gpio_id);
	    gpio_set_direction(self->gpio_id, GPIO_MODE_INPUT);
	    gpio_set_pull_mode(self->gpio_id, GPIO_FLOATING);
	}
	self->gpio_id = -1;
	return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_deinit_obj, madc_deinit);

//---------------------------------------------------------------------------------------
STATIC void madc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    madc_obj_t *self = self_in;

    if (self->gpio_id < 0) {
    	mp_printf(print, "ADC( deinitialized )");
    	return;
    }

    char satten[16];
    char spin[8];
    if (self->atten == ADC_ATTEN_DB_0) sprintf(satten, "0dB (1.1V)");
    else if (self->atten == ADC_ATTEN_DB_2_5) sprintf(satten, "2.5dB (1.5V)");
    else if (self->atten == ADC_ATTEN_DB_6) sprintf(satten, "6dB (2.5V)");
    else if (self->atten == ADC_ATTEN_DB_11) sprintf(satten, "11dB (3.9V)");
    else sprintf(satten, "Unknown");

    if (self->gpio_id == GPIO_NUM_MAX) sprintf(spin, "HALL");
    else sprintf(spin, "Pin(%u)", self->gpio_id);

    mp_printf(print, "ADC(%s: unit=ADC%d, chan=%d, width=%u bits, atten=%s, Vref=%u mV)", spin, self->adc_num, self->adc_chan, self->width+9, satten, adc_vref);
}

//----------------------------------------------
STATIC mp_obj_t madc_readraw(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
	if (self->gpio_id < 0) {
		mp_raise_ValueError("Not initialized");
	}

	int val = 0;
	if (self->adc_num == ADC_UNIT_1) {
		set_width(self);

		if (self->gpio_id == GPIO_NUM_MAX) val= hall_sensor_read();
		else val = adc1_get_raw(self->adc_chan);
		if (val == -1) mp_raise_ValueError("Parameter Error (ADC raw read)");
	}
	else {
		if (last_atten2 != self->atten) {
			adc2_config_channel_atten(self->adc_chan, self->atten);
			last_atten2 = self->atten;
		}
		esp_err_t err = adc2_get_raw(self->adc_chan, self->atten, &val);
		if (err != ESP_OK) mp_raise_ValueError("Cannot read, ADC2 used by Wi-Fi");
	}

    return MP_OBJ_NEW_SMALL_INT(val);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_readraw_obj, madc_readraw);

//-------------------------------------------
STATIC mp_obj_t madc_read(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
	if (self->gpio_id < 0) {
		mp_raise_ValueError("Not initialized");
	}

    set_width(self);

    int adc_val = 0;
    if (self->gpio_id == GPIO_NUM_MAX) adc_val= hall_sensor_read();
    else {
    	if ((last_adc_num != self->adc_num) || (last_adc_vref != adc_vref) || (last_atten != self->atten) || (last_adc_width != self->width)) {
    		// New characterization needed
    		esp_adc_cal_value_t cal_val = esp_adc_cal_characterize(self->adc_num, self->atten, self->width, adc_vref, &characteristics);
        	last_adc_vref = adc_vref;
        	last_atten = self->atten;
        	last_adc_width = self->width;
        	last_adc_num = self->adc_num;

    		ESP_LOGD("MOD_ADC", "Characterize ADC_UNIT_%d: Vref used: %s", self->adc_num, (cal_val == 0) ? "eFuse" : (cal_val == 1) ? "Two point" : "Default");
    	}
		if ((self->adc_num == ADC_UNIT_2) && (last_atten2 != self->atten)) {
			adc2_config_channel_atten(self->adc_chan, self->atten);
			last_atten2 = self->atten;
		}
    	esp_err_t err = esp_adc_cal_get_voltage(self->adc_chan, &characteristics, (uint32_t *)&adc_val);
		if (err != ESP_OK) {
			if (self->adc_num == ADC_UNIT_2) mp_raise_ValueError("Cannot read, ADC2 used by Wi-Fi");
			else mp_raise_ValueError("Error reading");
		}
    }
    return MP_OBJ_NEW_SMALL_INT(adc_val);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_read_obj, madc_read);

//---------------------------------------------------------------
STATIC mp_obj_t madc_atten(mp_obj_t self_in, mp_obj_t atten_in) {
    madc_obj_t *self = self_in;
	if (self->gpio_id < 0) {
		mp_raise_ValueError("Not initialized");
	}

	if (self->gpio_id == GPIO_NUM_MAX) return mp_const_none;

    adc_atten_t atten = mp_obj_get_int(atten_in);
    if ((atten < ADC_ATTEN_DB_0) || (atten > ADC_ATTEN_DB_11)) mp_raise_ValueError("Unsupported atten value");

    esp_err_t err;
	if (self->adc_num == ADC_UNIT_1) {
		err = adc1_config_channel_atten(self->adc_chan, atten);
		if (err != ESP_OK) mp_raise_ValueError("Parameter Error (config attenuation)");
	}
	else {
		if (last_atten2 != atten) {
			adc2_config_channel_atten(self->adc_chan, atten);
			last_atten2 = self->atten;
		}
	}
    self->atten = atten;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(madc_atten_obj, madc_atten);

//---------------------------------------------------------------
STATIC mp_obj_t madc_width(mp_obj_t self_in, mp_obj_t width_in) {
	madc_obj_t *self = self_in;
	if (self->gpio_id < 0) {
		mp_raise_ValueError("Not initialized");
	}

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
    { MP_ROM_QSTR(MP_QSTR_deinit),		MP_ROM_PTR(&madc_deinit_obj) },

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
