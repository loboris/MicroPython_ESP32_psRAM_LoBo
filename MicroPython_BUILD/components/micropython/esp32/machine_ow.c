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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "libs/esp_rmt.h"
#include "libs/ow/owb.h"
#include "libs/ow/owb_rmt.h"
#include "libs/ow/ds18b20.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "modmachine.h"


#define MAX_DEVICES          (8)
#define DS18B20_RESOLUTION   (DS18B20_RESOLUTION_12_BIT)
#define SAMPLE_PERIOD        (1000)   // milliseconds

typedef struct _onewire_obj_t {
    mp_obj_base_t base;
    rmt_channel_t tx_channel;
    rmt_channel_t rx_channel;
    int gpio_num;
    int num_devices;
    int n_used;
    OneWireBus_ROMCode device_rom_codes[MAX_DEVICES];
    void *used_by[MAX_DEVICES];
    owb_rmt_driver_info rmt_driver_info;
    OneWireBus *owb;
} onewire_obj_t;

typedef struct _ds18x20_obj_t {
    mp_obj_base_t base;
    int device;
    onewire_obj_t *owb_obj;
    DS18B20_Info *info;
} ds18x20_obj_t;

uint64_t conv_end_time = 0;

//--------------------------------------------
static void ow_obj_search(onewire_obj_t *self)
{
    memset(self->device_rom_codes, 0, sizeof(self->device_rom_codes));
    self->num_devices = 0;

    OneWireBus_SearchState search_state = {0};
    bool found = false;

    int res = owb_search_first(self->owb, &search_state, &found);
    if (res != OWB_STATUS_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error searching Onewire devices"));
    }

    while (found) {
        self->device_rom_codes[self->num_devices] = search_state.rom_code;
        self->num_devices++;
        res = owb_search_next(self->owb, &search_state, &found);
        if (res != OWB_STATUS_OK) break;
    }
}

//------------------------------------------------------------------------------------------------
STATIC void machine_onewire_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    onewire_obj_t *self = self_in;

    if (self->owb == NULL) {
    	mp_printf(print, "Onewire( deinit )\n");
    	return;
    }
	mp_printf(print, "Onewire(Pin=%d, RMTChannels=%d&%d, Devices=%d, Used=%d)\n",
			self->gpio_num, self->tx_channel, self->rx_channel, self->num_devices, self->n_used);
}

//-------------------------------------
void _check_ow_obj(onewire_obj_t *self)
{
    if (self->owb == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Onewire object: deinitialized"));
    }
}

//-----------------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_onewire_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	//-----------------------------------------------------
	const mp_arg_t machine_neopixel_init_allowed_args[] = {
			{ MP_QSTR_pin,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
	};

	mp_arg_val_t args[MP_ARRAY_SIZE(machine_neopixel_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(machine_neopixel_init_allowed_args), machine_neopixel_init_allowed_args, args);

    int tx_rmtchan = platform_rmt_allocate(1);
    if (tx_rmtchan < 0) {
    	mp_raise_ValueError("Cannot acquire tx RMT channel");
    }
    int rx_rmtchan = platform_rmt_allocate(1);
    if (rx_rmtchan < 0) {
        platform_rmt_release(tx_rmtchan);
    	mp_raise_ValueError("Cannot acquire rx RMT channel");
    }

    int8_t pin = machine_pin_get_gpio(args[0].u_obj);
    // ToDo: Check valid pin (not input only)

    // Check pin
    if (pin > 33) {
    	mp_raise_ValueError("Wrong pin, only pins 0~33 allowed");
    }

    // Setup the Onewire object
    onewire_obj_t *self = m_new_obj(onewire_obj_t );

    self->tx_channel = tx_rmtchan;
    self->rx_channel = rx_rmtchan;
    self->gpio_num = pin;
    self->n_used = 0;

    memset(self->device_rom_codes, 0, sizeof(self->device_rom_codes));
    memset(self->used_by, 0, sizeof(self->used_by));

    self->base.type = &machine_onewire_type;

    self->owb = owb_rmt_initialize(&self->rmt_driver_info, self->gpio_num, self->tx_channel, self->rx_channel);

    if (owb_use_crc(self->owb, true) != OWB_STATUS_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error initializing Onewire interface"));
    }

    ow_obj_search(self);

    return MP_OBJ_FROM_PTR(self);
}

//--------------------------------------------------------
STATIC mp_obj_t machine_onewire_deinit(mp_obj_t self_in) {
    onewire_obj_t *self = self_in;

    _check_ow_obj(self);

    bool is_used = false;
	for (int i=0; i<MAX_DEVICES; i++) {
		if (self->used_by[i] != NULL) {
			is_used = true;
			break;
		}
	}
	if (is_used) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Some devices uses this ow bus"));
	}

	owb_uninitialize(self->owb);
    platform_rmt_release(self->rx_channel);
    platform_rmt_release(self->tx_channel);
    self->owb = NULL;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_onewire_deinit_obj, machine_onewire_deinit);

//-------------------------------------------------------
STATIC mp_obj_t machine_onewire_reset(mp_obj_t self_in) {
    onewire_obj_t *self = self_in;

    _check_ow_obj(self);

    bool res = false;
    owb_reset(self->owb, &res);

    return mp_obj_new_bool(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_onewire_reset_obj, machine_onewire_reset);

//----------------------------------------------------------
STATIC mp_obj_t machine_onewire_readbyte(mp_obj_t self_in) {
    onewire_obj_t *self = self_in;

    _check_ow_obj(self);

    uint8_t value = 0;
    owb_read_byte(self->owb, &value);

    return MP_OBJ_NEW_SMALL_INT(value);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_onewire_readbyte_obj, machine_onewire_readbyte);

//----------------------------------------------------------------------------
STATIC mp_obj_t machine_onewire_readbytes(mp_obj_t self_in, mp_obj_t len_in) {
    onewire_obj_t *self = self_in;

    _check_ow_obj(self);

    int len = mp_obj_get_int(len_in) & 0xFF;
    uint8_t buff[len];

    owb_read_bytes(self->owb, buff, len);

    return mp_obj_new_str((const char *)buff, len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_onewire_readbytes_obj, machine_onewire_readbytes);

//------------------------------------------------------------------------------
STATIC mp_obj_t machine_onewire_writebyte(mp_obj_t self_in, mp_obj_t value_in) {
    onewire_obj_t *self = self_in;

    _check_ow_obj(self);

    int value = mp_obj_get_int(value_in) & 0xFF;

    owb_write_byte(self->owb, (uint8_t)value);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_onewire_writebyte_obj, machine_onewire_writebyte);

//-----------------------------------------------------------------------------
STATIC mp_obj_t machine_onewire_writebytes(mp_obj_t self_in, mp_obj_t buf_in) {
    onewire_obj_t *self = self_in;

    _check_ow_obj(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    owb_write_bytes(self->owb, bufinfo.buf, bufinfo.len);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_onewire_writebytes_obj, machine_onewire_writebytes);

//---------------------------------------------------------------------
STATIC mp_obj_t machine_onewire_crc8(mp_obj_t self_in, mp_obj_t data) {
    onewire_obj_t *self = self_in;

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);

    _check_ow_obj(self);

    uint8_t crc = owb_crc8_bytes(0, (uint8_t*)bufinfo.buf, bufinfo.len);

    return MP_OBJ_NEW_SMALL_INT(crc);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_onewire_crc8_obj, machine_onewire_crc8);

//------------------------------------------------------------------------------
STATIC mp_obj_t machine_onewire_rom_code(mp_obj_t self_in, mp_obj_t device_in) {
    onewire_obj_t *self = self_in;

    _check_ow_obj(self);

    int device = mp_obj_get_int(device_in) & 0xFF;
    if ((device < 0) ||(device > (self->num_devices-1))) {
    	mp_raise_ValueError("Wrong device requested");
    }

    char rom_code[17];
	owb_string_from_rom_code(self->device_rom_codes[device], rom_code, sizeof(rom_code));

    return mp_obj_new_str(rom_code, strlen(rom_code));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_onewire_rom_code_obj, machine_onewire_rom_code);

//--------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_onewire_search(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_asnum,  MP_ARG_BOOL, { .u_bool = false } },
    };
    onewire_obj_t *self = pos_args[0];

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _check_ow_obj(self);

    ow_obj_search(self);

    if (self->num_devices == 0) return mp_const_none;

    char rom_code_s[17];
   	mp_obj_t tuple[self->num_devices];
   	mp_obj_t code_tuple[3];

   	for (int i=0; i<self->num_devices; i++) {
   		if (args[0].u_bool) {
   			mp_int_t ser_num = 0;
   			for (int idx=0; idx<6; idx++) {
   				ser_num |= (uint64_t)self->device_rom_codes[i].bytes[idx+1] << (idx*8);
   			}
   			code_tuple[0] = mp_obj_new_int(self->device_rom_codes[i].fields.family[0]);
   			code_tuple[1] = mp_obj_new_int_from_ll(ser_num);
   			code_tuple[2] = mp_obj_new_int(self->device_rom_codes[i].fields.crc[0]);
   			tuple[i] = mp_obj_new_tuple(3, code_tuple);
   		}
   		else {
			owb_string_from_rom_code(self->device_rom_codes[i], rom_code_s, sizeof(rom_code_s));
			tuple[i] = mp_obj_new_str(rom_code_s, strlen(rom_code_s));
   		}
   	}

   	return mp_obj_new_tuple(self->num_devices, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_onewire_search_obj, 0, machine_onewire_search);


// ==== DS18B20 ================================================================

//---------------------------------------------------
static void _check_ow_conversion(ds18x20_obj_t *self)
{
	if (self->info == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "ds18x20 object: deinitialized"));
	}
	if (self->owb_obj->owb == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Onewire object is deinitialized"));
	}
	if (conv_end_time) {
		// Conversion was started, wait until it is finished
	    int max_conv_time = -10;
	    while (mp_hal_ticks_ms() < conv_end_time) {
	        vTaskDelay(10 / portTICK_PERIOD_MS);
	        max_conv_time += 10;
	        // max wait time in case of overflow
	        if (max_conv_time > T_CONV) break;
	    }
    	conv_end_time = 0;
	}
}

//------------------------------------------------------------------------------------------------
STATIC void machine_ds18x20_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    ds18x20_obj_t *self = self_in;
	if (self->info == NULL) {
		mp_printf(print, "ds18x20( deinit )\n");
		return;
	}
	if (self->owb_obj->owb == NULL) {
		mp_printf(print, "ds18x20( Onewire object is deinitialized )\n");
		return;
	}

	char dsfamily[16] = {'\0'};
    char rom_code[17];
	owb_string_from_rom_code(self->owb_obj->device_rom_codes[self->device], rom_code, sizeof(rom_code));

	DS18B20_Family(self->owb_obj->device_rom_codes[self->device].fields.family[0], dsfamily);
	mp_printf(print, "ds18x20(Pin=%d, OW Device=%d, Type: %s, Resolution=%dbits, ROM code=%s, Parasite power: %s)\n",
			self->owb_obj->gpio_num, self->device, dsfamily, self->info->resolution, rom_code, (self->info->bus->parasite_pwr ? "True" : "False"));
}

//-----------------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	//-----------------------------------------------------
	const mp_arg_t machine_neopixel_init_allowed_args[] = {
			{ MP_QSTR_owb,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
			{ MP_QSTR_device,  MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
	};

	mp_arg_val_t args[MP_ARRAY_SIZE(machine_neopixel_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(machine_neopixel_init_allowed_args), machine_neopixel_init_allowed_args, args);


    if (!MP_OBJ_IS_TYPE(args[0].u_obj, &machine_onewire_type)) {
    	mp_raise_ValueError("Onewire object expected");
    }

    onewire_obj_t *owb = args[0].u_obj;
    int device = args[1].u_int;

    if (owb->num_devices == 0) {
    	mp_raise_ValueError("No devices registered on Onewire bus");
    }
    if ((device < 0) ||(device > (owb->num_devices-1))) {
    	mp_raise_ValueError("Wrong device requested");
    }
    if (_is_DS18B20_family(owb->device_rom_codes[device].fields.family[0]) == 0) {
    	mp_raise_ValueError("Requested device is not from DS18x20 family");
    }
    // Setup the DS18x20 object
    ds18x20_obj_t *self = m_new_obj(ds18x20_obj_t );

    self->base.type = &machine_ds18x20_type;
    self->device = device;
    self->owb_obj = owb;
    self->info = ds18b20_malloc();  // heap allocation
    if (self->info == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating DS28x20 info"));
    }
    self->info->bus = owb->owb;
    self->info->resolution = DS18B20_RESOLUTION_12_BIT;

    _check_ow_conversion(self);

    if (owb->num_devices == 1) ds18b20_init_solo(self->info, owb->owb);		// only one device on bus
    else ds18b20_init(self->info, owb->owb, owb->device_rom_codes[device]);	// associate with bus and device
    ds18b20_use_crc(self->info, true);										// enable CRC check for temperature readings

    owb->n_used++;
    owb->used_by[device] = (void *)self;

    uint8_t ppwr = ds18b20_get_power_mode(self->info);
    if (ppwr == 0) owb->owb->parasite_pwr = true;
    else owb->owb->parasite_pwr = false;
    owb->owb->gpio = owb->rmt_driver_info.gpio;

    return MP_OBJ_FROM_PTR(self);
}

//--------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_deinit(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    ds18b20_free(&self->info);
    self->owb_obj->used_by[self->device] = NULL;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_deinit_obj, machine_ds18x20_deinit);

//----------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_readtemp(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    float temper = 0;
    temper = ds18b20_read_temp(self->info);
	if (temper == DS18B20_INVALID_READING) return mp_const_none;

    return mp_obj_new_float(temper);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_readtemp_obj, machine_ds18x20_readtemp);

//--------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_readtemp_raw(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    int16_t temper = ds18b20_read_raw_temp(self->info);

    return MP_OBJ_NEW_SMALL_INT(temper);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_readtemp_raw_obj, machine_ds18x20_readtemp_raw);

//--------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_convert_read(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    float temper = ds18b20_convert_and_read_temp(self->info);
	if (temper == DS18B20_INVALID_READING) return mp_const_none;

    return mp_obj_new_float(temper);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_convert_read_obj, machine_ds18x20_convert_read);

//------------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_convert_read_raw(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    int16_t temper = ds18b20_convert_and_read_raw_temp(self->info);

    return MP_OBJ_NEW_SMALL_INT(temper);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_convert_read_raw_obj, machine_ds18x20_convert_read_raw);

//---------------------------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_start_convert(mp_obj_t self_in, mp_obj_t wait_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    int wait = mp_obj_get_int(wait_in) & 1;
	// Find maximal resolution of all devices on the bus
	int res = 0;
	for (int i=0; i<MAX_DEVICES; i++) {
		if (self->owb_obj->used_by[i] != NULL) {
		    if (MP_OBJ_IS_TYPE((mp_obj_t)self->owb_obj->used_by[i], &machine_ds18x20_type)) {
		    	ds18x20_obj_t *ds_obj = self->owb_obj->used_by[i];
		        if (_is_DS18B20_family(ds_obj->owb_obj->device_rom_codes[i].fields.family[0])) {
		        	if (ds_obj->info->resolution > res) res = ds_obj->info->resolution;
		        }
		    }
		}
	}

    ds18b20_convert_all(self->info);

    uint64_t max_conversion_time = (T_CONV >> (DS18B20_RESOLUTION_12_BIT - res)) + 2;
	conv_end_time = mp_hal_ticks_ms() + max_conversion_time;

	if (wait) _check_ow_conversion(self);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ds18x20_start_convert_obj, machine_ds18x20_start_convert);

//--------------------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_set_res(mp_obj_t self_in, mp_obj_t res_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    int res = mp_obj_get_int(res_in) & 0x0F;
    if ((res < DS18B20_RESOLUTION_9_BIT) || (res > DS18B20_RESOLUTION_12_BIT)) {
    	mp_raise_ValueError("Expected resolution 9 ~ 12");
    }

    if (ds18b20_set_resolution(self->info, res)) return mp_const_true;

    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_ds18x20_set_res_obj, machine_ds18x20_set_res);

//---------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_get_res(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    int res = ds18b20_read_resolution(self->info);

    return MP_OBJ_NEW_SMALL_INT(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_get_res_obj, machine_ds18x20_get_res);

//-------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_get_pwrmode(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    _check_ow_conversion(self);

    int res = ds18b20_get_power_mode(self->info);

    return MP_OBJ_NEW_SMALL_INT(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_get_pwrmode_obj, machine_ds18x20_get_pwrmode);

//--------------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_get_convtime(mp_obj_t self_in) {
    int64_t conv_time = 0;
	if (conv_end_time) {
	    conv_time = mp_hal_ticks_ms() - conv_end_time;
	}

    return mp_obj_new_int(conv_time);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_get_convtime_obj, machine_ds18x20_get_convtime);

//----------------------------------------------------------
STATIC mp_obj_t machine_ds18x20_rom_code(mp_obj_t self_in) {
    ds18x20_obj_t *self = self_in;

    char rom_code[17];
	owb_string_from_rom_code(self->owb_obj->device_rom_codes[self->device], rom_code, sizeof(rom_code));

    return mp_obj_new_str(rom_code, strlen(rom_code));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_ds18x20_rom_code_obj, machine_ds18x20_rom_code);


//====================================================================
STATIC const mp_rom_map_elem_t machine_ds18x20_locals_dict_table[] = {
	{ MP_ROM_QSTR(MP_QSTR_read_temp),		(mp_obj_t)&machine_ds18x20_readtemp_obj },
	{ MP_ROM_QSTR(MP_QSTR_convert_read),	(mp_obj_t)&machine_ds18x20_convert_read_obj },
	{ MP_ROM_QSTR(MP_QSTR_read_tempint),	(mp_obj_t)&machine_ds18x20_readtemp_raw_obj },
	{ MP_ROM_QSTR(MP_QSTR_convert_readint),	(mp_obj_t)&machine_ds18x20_convert_read_raw_obj },
	{ MP_ROM_QSTR(MP_QSTR_convert),			(mp_obj_t)&machine_ds18x20_start_convert_obj },
	{ MP_ROM_QSTR(MP_QSTR_set_res),			(mp_obj_t)&machine_ds18x20_set_res_obj },
	{ MP_ROM_QSTR(MP_QSTR_get_res),			(mp_obj_t)&machine_ds18x20_get_res_obj },
	{ MP_ROM_QSTR(MP_QSTR_rom_code),		(mp_obj_t)&machine_ds18x20_rom_code_obj },
	{ MP_ROM_QSTR(MP_QSTR_get_pwrmode),		(mp_obj_t)&machine_ds18x20_get_pwrmode_obj },
	{ MP_ROM_QSTR(MP_QSTR_conv_time),		(mp_obj_t)&machine_ds18x20_get_convtime_obj },
	{ MP_ROM_QSTR(MP_QSTR_deinit),			(mp_obj_t)&machine_ds18x20_deinit_obj },
};

STATIC MP_DEFINE_CONST_DICT(machine_ds18x20_locals_dict, machine_ds18x20_locals_dict_table);

//==========================================
const mp_obj_type_t machine_ds18x20_type = {
    { &mp_type_type },
    .name = MP_QSTR_ds18x20,
    .print = machine_ds18x20_print,
    .make_new = machine_ds18x20_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_ds18x20_locals_dict,
};

// ==== DS18B20 ================================================================


//====================================================================
STATIC const mp_rom_map_elem_t machine_onewire_locals_dict_table[] = {
	{ MP_ROM_QSTR(MP_QSTR_reset),		(mp_obj_t)&machine_onewire_reset_obj },
	{ MP_ROM_QSTR(MP_QSTR_readbyte),	(mp_obj_t)&machine_onewire_readbyte_obj },
	{ MP_ROM_QSTR(MP_QSTR_writebyte),	(mp_obj_t)&machine_onewire_writebyte_obj },
	{ MP_ROM_QSTR(MP_QSTR_readbytes),	(mp_obj_t)&machine_onewire_readbytes_obj },
	{ MP_ROM_QSTR(MP_QSTR_writebytes),	(mp_obj_t)&machine_onewire_writebytes_obj },
	{ MP_ROM_QSTR(MP_QSTR_search),		(mp_obj_t)&machine_onewire_search_obj },
	{ MP_ROM_QSTR(MP_QSTR_scan),		(mp_obj_t)&machine_onewire_search_obj },
	{ MP_ROM_QSTR(MP_QSTR_crc8),		(mp_obj_t)&machine_onewire_crc8_obj },
	{ MP_ROM_QSTR(MP_QSTR_rom_code),	(mp_obj_t)&machine_onewire_rom_code_obj },
	{ MP_ROM_QSTR(MP_QSTR_deinit),		(mp_obj_t)&machine_onewire_deinit_obj },

	{ MP_OBJ_NEW_QSTR(MP_QSTR_ds18x20),	MP_ROM_PTR(&machine_ds18x20_type) },
};

STATIC MP_DEFINE_CONST_DICT(machine_onewire_locals_dict, machine_onewire_locals_dict_table);

//==========================================
const mp_obj_type_t machine_onewire_type = {
    { &mp_type_type },
    .name = MP_QSTR_Onewire,
    .print = machine_onewire_print,
    .make_new = machine_onewire_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_onewire_locals_dict,
};

