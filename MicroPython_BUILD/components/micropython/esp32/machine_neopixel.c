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

/*
 * Module for the Neopixel/WS2812 RGB LEDs using the RMT peripheral on the ESP32.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "libs/esp_rmt.h"
#include "libs/neopixel.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "mphalport.h"


typedef struct _machine_neopixel_obj_t {
    mp_obj_base_t base;
    rmt_channel_t channel;
    int gpio_num;
    pixel_settings_t px;
} machine_neopixel_obj_t;


//------------------------------------------------
STATIC void np_check(machine_neopixel_obj_t *self)
{
    if (self->px.pixels == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Neopixel instance not initialized"));
    }
}

//-----------------------------------------------------------------------------------------------
STATIC void machine_neopixel_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    machine_neopixel_obj_t *self = self_in;

    if (self->px.pixels != NULL) {
        char colorder[5];
        sprintf(colorder, self->px.color_order);
        if (self->px.nbits == 24) colorder[3] = '\0';
		mp_printf(print, "Neopixel(Pin=%d, Pixels: %d, bit/pix=%d, RMTChannel=%d, PixBufLen=%u, Color order: '%s'\n",
				self->gpio_num, self->px.pixel_count, self->px.nbits, self->channel, sizeof(uint32_t) * self->px.pixel_count, colorder);
		mp_printf(print, "         Timings (ns): T1H=%d, T1L=%d, T0H=%d, T0L=%d, Treset=%d\n)",
				self->px.timings.mark.duration0 * RMT_PERIOD_NS, self->px.timings.mark.duration1 * RMT_PERIOD_NS,
				self->px.timings.space.duration0 * RMT_PERIOD_NS, self->px.timings.space.duration1 * RMT_PERIOD_NS,
				(self->px.timings.reset.duration0 + self->px.timings.reset.duration1) * RMT_PERIOD_NS);
    }
    else {
		mp_printf(print, "Neopixel(Not initialized)");
    }
}

//------------------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	enum { ARG_pin, ARG_pixels, ARG_type };
	//-----------------------------------------------------
	const mp_arg_t machine_neopixel_init_allowed_args[] = {
			{ MP_QSTR_pin,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
			{ MP_QSTR_pixels,  MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 4} },
			{ MP_QSTR_type,                      MP_ARG_INT, {.u_int = 0} },
	};

	mp_arg_val_t args[MP_ARRAY_SIZE(machine_neopixel_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(machine_neopixel_init_allowed_args), machine_neopixel_init_allowed_args, args);

    int rmtchan = platform_rmt_allocate(1);
    if (rmtchan < 0) {
    	mp_raise_ValueError("Cannot acquire RMT channel");
    }

    int8_t pin = machine_pin_get_gpio(args[ARG_pin].u_obj);

    int pixels = args[ARG_pixels].u_int;
    int wstype = args[ARG_type].u_int;

    // Check type
    if ((wstype < 0) || (wstype > 1)) {
    	mp_raise_ValueError("Wrong type");
    }
    // Check pin
    if (pin > 31) {
    	mp_raise_ValueError("Wrong pin, only pins 0~31 allowed");
    }
    // Check number of pixels
    if ((pixels < 0) || (pixels > 1024)) {
    	mp_raise_ValueError("Maximum 1024 pixels can be used");
    }

    // Setup the neopixels object
    machine_neopixel_obj_t *self = m_new_obj(machine_neopixel_obj_t );

    self->channel = rmtchan;
    self->gpio_num = pin;
    self->px.pixel_count = pixels;

    // Set defaults
    self->px.brightness = 255;
    sprintf(self->px.color_order, "GRBW");

	self->px.timings.mark.level0 = 1;
	self->px.timings.mark.level1 = 0;
	self->px.timings.space.level0 = 1;
	self->px.timings.space.level1 = 0;
	self->px.timings.reset.level0 = 0;
	self->px.timings.reset.level1 = 0;
	self->px.timings.mark.duration0 = 12;

    if (wstype == 1) {
    	self->px.nbits = 32;
    	self->px.timings.mark.duration1 = 12;
    	self->px.timings.space.duration0 = 6;
    	self->px.timings.space.duration1 = 18;
    	self->px.timings.reset.duration0 = 900;
    	self->px.timings.reset.duration1 = 900;
    }
    else {
    	self->px.nbits = 24;
    	self->px.timings.mark.duration1 = 14;
    	self->px.timings.space.duration0 = 7;
    	self->px.timings.space.duration1 = 16;
    	self->px.timings.reset.duration0 = 600;
    	self->px.timings.reset.duration1 = 600;
    }

    // Allocate buffers
    self->px.pixels = (uint8_t *)malloc((self->px.nbits/8) * self->px.pixel_count);
    if (self->px.pixels == NULL) goto error_exit;

    if (neopixel_init(pin, rmtchan) != ESP_OK) goto error_exit;

    self->base.type = &machine_neopixel_type;

	np_clear(&self->px);
	np_show(&self->px, self->channel);

    return MP_OBJ_FROM_PTR(self);

error_exit:
	platform_rmt_release(rmtchan);
	if (self->px.pixels) free(self->px.pixels);
	self->px.pixels = NULL;

    nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error initializing Neopixel interface"));
	return mp_const_none;
}

//------------------------------------------------------
STATIC mp_obj_t machine_neopixel_deinit(mp_obj_t self_in)
{
    machine_neopixel_obj_t *self = self_in;
    np_check(self);

	np_clear(&self->px);
	np_show(&self->px, self->channel);

    neopixel_deinit(self->channel);

    if (self->px.pixels) free(self->px.pixels);
    self->px.pixels = NULL;

    platform_rmt_release(self->channel);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_neopixel_deinit_obj, machine_neopixel_deinit);

//------------------------------------------------------
STATIC mp_obj_t machine_neopixel_clear(mp_obj_t self_in)
{
    machine_neopixel_obj_t *self = self_in;
    np_check(self);

	np_clear(&self->px);
   	MP_THREAD_GIL_EXIT();
	np_show(&self->px, self->channel);
   	MP_THREAD_GIL_ENTER();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_neopixel_clear_obj, machine_neopixel_clear);

//-----------------------------------------------------
STATIC mp_obj_t machine_neopixel_show(mp_obj_t self_in)
{
    machine_neopixel_obj_t *self = self_in;
    np_check(self);

   	MP_THREAD_GIL_EXIT();
	np_show(&self->px, self->channel);
   	MP_THREAD_GIL_ENTER();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_neopixel_show_obj, machine_neopixel_show);

//----------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_set(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	const mp_arg_t allowed_args[] = {
	    { MP_QSTR_pos,   MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 1} },
	    { MP_QSTR_color, MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
	    { MP_QSTR_white,                   MP_ARG_INT,  {.u_int = 0} },
	    { MP_QSTR_num,                     MP_ARG_INT,  {.u_int = 1} },
	    { MP_QSTR_update,                  MP_ARG_BOOL, {.u_bool = true} },
	};
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int cnt = args[3].u_int;
    uint32_t color = (uint32_t)args[1].u_int << 8;
    int pos = args[0].u_int;

    if (self->px.nbits == 32) {
    	color |= (args[2].u_int & 0x000000FF);
    }

    if (pos < 1) pos = 1;
	if (pos > self->px.pixel_count) pos = self->px.pixel_count;
	if (cnt < 1) cnt = 1;
	if ((cnt + pos - 1) > self->px.pixel_count) cnt = self->px.pixel_count - pos + 1;

	for (uint16_t i = 0; i < cnt; i++) {
		np_set_pixel_color(&self->px, i+pos-1, color);
	}

	if (args[4].u_bool) {
	   	MP_THREAD_GIL_EXIT();
		np_show(&self->px, self->channel);
	   	MP_THREAD_GIL_ENTER();
	}

	return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_set_obj, 3, machine_neopixel_set);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_set_white(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	const mp_arg_t allowed_args[] = {
	    { MP_QSTR_pos,   MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 1} },
	    { MP_QSTR_white, MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
	    { MP_QSTR_num,                     MP_ARG_INT,  {.u_int = 1} },
	    { MP_QSTR_update,                  MP_ARG_BOOL, {.u_bool = true} },
	};
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (self->px.nbits == 32) {
		int pos = args[0].u_int;
		int cnt = args[2].u_int;
		if (pos < 1) pos = 1;
		if (pos > self->px.pixel_count) pos = self->px.pixel_count;
		if (cnt < 1) cnt = 1;
		if ((cnt + pos - 1) > self->px.pixel_count) cnt = self->px.pixel_count - pos + 1;

		uint8_t white;
		uint32_t color = np_get_pixel_color(&self->px, pos-1, &white) << 8;

		color |= (args[1].u_int & 0x000000FF);

		for (uint16_t i = 0; i < cnt; i++) {
			np_set_pixel_color(&self->px, i+pos-1, color);
		}

		if (args[3].u_bool) {
			MP_THREAD_GIL_EXIT();
			np_show(&self->px, self->channel);
			MP_THREAD_GIL_ENTER();
		}
    }

	return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_set_white_obj, 3, machine_neopixel_set_white);

//---------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_setHSB(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
  	    { MP_QSTR_pos, 		   MP_ARG_REQUIRED | MP_ARG_INT,  { .u_int = 0} },
        { MP_QSTR_hue,         MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_saturation,  MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_brightness,  MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
	    { MP_QSTR_num,                           MP_ARG_INT,  { .u_int = 1} },
	    { MP_QSTR_update,                        MP_ARG_BOOL, { .u_bool = true} },
    };
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_float_t hue = mp_obj_get_float(args[1].u_obj);
    mp_float_t sat = mp_obj_get_float(args[2].u_obj);
    mp_float_t bri = mp_obj_get_float(args[3].u_obj);

    uint32_t color = hsb_to_rgb(hue, sat, bri) << 8;

    int pos = args[0].u_int;
    int cnt = args[4].u_int;

	if (pos < 1) pos = 1;
	if (pos > self->px.pixel_count) pos = self->px.pixel_count;
	if (cnt < 1) cnt = 1;
	if ((cnt + pos - 1) > self->px.pixel_count) cnt = self->px.pixel_count - pos + 1;

	for (uint16_t i = 0; i < cnt; i++) {
		np_set_pixel_color(&self->px, i+pos-1, color);
	}

	if (args[5].u_bool) {
	   	MP_THREAD_GIL_EXIT();
		np_show(&self->px, self->channel);
	   	MP_THREAD_GIL_ENTER();
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_setHSB_obj, 5, machine_neopixel_setHSB);

//-------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_setHSB_int(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
  	    { MP_QSTR_pos, 		   MP_ARG_REQUIRED | MP_ARG_INT,  { .u_int = 0} },
        { MP_QSTR_hue,         MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_saturation,  MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_brightness,  MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
	    { MP_QSTR_num,                           MP_ARG_INT,  { .u_int = 1} },
	    { MP_QSTR_update,                        MP_ARG_BOOL, { .u_bool = true} },
    };
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int hue = mp_obj_get_int(args[1].u_obj) & 0xFF;
    int sat = mp_obj_get_int(args[2].u_obj) & 0xFF;
    int bri = mp_obj_get_int(args[3].u_obj) & 0xFF;

    if (hue < 0) hue = 0;
    if (sat < 0) sat = 0;
    if (bri < 0) bri = 0;
    if (sat > 1000) sat = 1000;
    if (bri > 1000) bri = 1000;

    uint32_t color = hsb_to_rgb_int(hue, sat, bri) << 8;

    int pos = args[0].u_int;
    int cnt = args[4].u_int;

	if (pos < 1) pos = 1;
	if (pos > self->px.pixel_count) pos = self->px.pixel_count;
	if (cnt < 1) cnt = 1;
	if ((cnt + pos - 1) > self->px.pixel_count) cnt = self->px.pixel_count - pos + 1;

	for (uint16_t i = 0; i < cnt; i++) {
		np_set_pixel_color(&self->px, i+pos-1, color);
	}

	if (args[5].u_bool) {
	   	MP_THREAD_GIL_EXIT();
		np_show(&self->px, self->channel);
	   	MP_THREAD_GIL_ENTER();
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_setHSB_int_obj, 5, machine_neopixel_setHSB_int);

//-----------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_get(size_t n_args, const mp_obj_t *args)
{
    machine_neopixel_obj_t *self = args[0];
    np_check(self);

	uint8_t white;
	uint32_t icolor;
    int pos = mp_obj_get_int(args[1]);
    if (pos < 1) pos = 1;
    if (pos > self->px.pixel_count) pos = self->px.pixel_count;
    int cnt = 0;
    if (n_args > 2) {
    	cnt  = mp_obj_get_int(args[2]);
    	if (cnt < 1) cnt = 1;
    	if ((cnt + pos - 1) > self->px.pixel_count) cnt = self->px.pixel_count - pos + 1;
    }
    if (cnt < 2) {
		icolor = np_get_pixel_color(&self->px, pos-1, &white);
		if (self->px.nbits == 24) {
			return mp_obj_new_int(icolor);
		}
		else {
			mp_obj_t tuple[2];

			tuple[0] = mp_obj_new_int(icolor);
			tuple[1] = mp_obj_new_int(white);

			return mp_obj_new_tuple(2, tuple);
		}
    }
    else {
		mp_obj_t pix_tuple[cnt];
		mp_obj_t tuple[2];
		for (int i=0; i<cnt; i++) {
			icolor = np_get_pixel_color(&self->px, pos+i-1, &white);
			if (self->px.nbits == 24) {
				pix_tuple[i] = mp_obj_new_int(icolor);
			}
			else {
				tuple[0] = mp_obj_new_int(icolor);
				tuple[1] = mp_obj_new_int(white);

				pix_tuple[i] = mp_obj_new_tuple(2, tuple);
			}
		}
		return mp_obj_new_tuple(cnt, pix_tuple);
    }
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_neopixel_get_obj, 2, 3, machine_neopixel_get);

//-----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_brightness(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	const mp_arg_t allowed_args[] = {
	    { MP_QSTR_brightness,   MP_ARG_INT, {.u_int = -1} },
	    { MP_QSTR_update,       MP_ARG_BOOL, { .u_bool = true} },
	};
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int bright = args[0].u_int;
    if (bright > 0) {
    	self->px.brightness = bright & 0xFF;
    	if (args[1].u_bool) {
    	   	MP_THREAD_GIL_EXIT();
    		np_show(&self->px, self->channel);
    	   	MP_THREAD_GIL_ENTER();
    	}
    }
    return mp_obj_new_int(self->px.brightness);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_brightness_obj, 0, machine_neopixel_brightness);

//-----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_HSBtoRGB(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_hue,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_saturation,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_brightness,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_float_t hue = mp_obj_get_float(args[0].u_obj);
    mp_float_t sat = mp_obj_get_float(args[1].u_obj);
    mp_float_t bri = mp_obj_get_float(args[2].u_obj);

    uint32_t color = hsb_to_rgb(hue, sat, bri);

    return mp_obj_new_int(color);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_HSBtoRGB_obj, 4, machine_neopixel_HSBtoRGB);

//--------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_HSBtoRGBint(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_hue,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_saturation,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_brightness,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int hue = mp_obj_get_int(args[0].u_obj) & 0xFF;
    int sat = mp_obj_get_int(args[1].u_obj) & 0xFF;
    int bri = mp_obj_get_int(args[2].u_obj) & 0xFF;

    uint32_t color = hsb_to_rgb_int(hue, sat, bri);

    return mp_obj_new_int(color);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_HSBtoRGBint_obj, 4, machine_neopixel_HSBtoRGBint);

//------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_RGBtoHSB(mp_obj_t self_in, mp_obj_t color_in) {

    machine_neopixel_obj_t *self = self_in;
    np_check(self);

    uint32_t color = mp_obj_get_int(color_in);

    float hue, sat, bri;

    rgb_to_hsb(color, &hue, &sat, &bri);

   	mp_obj_t tuple[3];

   	tuple[0] = mp_obj_new_float(hue);
   	tuple[1] = mp_obj_new_float(sat);
   	tuple[2] = mp_obj_new_float(bri);

   	return mp_obj_new_tuple(3, tuple);
}
MP_DEFINE_CONST_FUN_OBJ_2(machine_neopixel_RGBtoHSB_obj, machine_neopixel_RGBtoHSB);

//---------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_corder(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_color_order,  MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char colorder[5];
    const char *corder = NULL;
    if (MP_OBJ_IS_STR(args[0].u_obj)) {
    	corder = mp_obj_str_get_str(args[0].u_obj);
    	// Check color order string
    	uint8_t str_ok = 0;
    	uint8_t nR = 0;
    	uint8_t nG = 0;
    	uint8_t nB = 0;
    	uint8_t nW = 0;
    	int len = strlen(corder);

    	if (((len == 3) && (self->px.nbits == 24)) || ((len == 4) && (self->px.nbits == 32))) {
    		for (int i=0; i < len; i++) {
    			if (corder[i] == 'R') nR++;
    			if (corder[i] == 'G') nG++;
    			if (corder[i] == 'B') nB++;
    			if (corder[i] == 'W') nW++;
    		}
    		if ((nR == 1) && (nG == 1) && (nB == 1)) {
    			str_ok = 1;
    			if ((len == 4) && (nW != 1)) str_ok = 0;
    		}
    	}
    	if (str_ok) {
    		// Set new color order string
   			sprintf(self->px.color_order, "%s", corder);
    	}
    	else {
        	mp_raise_ValueError("Wrong color order string");
    	}

    }
    sprintf(colorder, self->px.color_order);
    if (self->px.nbits == 24) colorder[3] = '\0';
    return mp_obj_new_str(colorder, strlen(colorder));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_corder_obj, 0, machine_neopixel_corder);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_timings(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_timings, MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
	machine_neopixel_obj_t *self = pos_args[0];
    np_check(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (MP_OBJ_IS_TYPE(args[0].u_obj, &mp_type_tuple)) {
        mp_obj_t *t_items;
        mp_obj_t *t1_items;
        mp_obj_t *t0_items;
        uint t_len, t1_len, t0_len;
        int rst;

        mp_obj_tuple_get(args[0].u_obj, &t_len, &t_items);
        if (t_len == 3) {
			mp_obj_tuple_get(t_items[0], &t1_len, &t1_items);
			mp_obj_tuple_get(t_items[1], &t0_len, &t0_items);
			rst = mp_obj_get_int(t_items[2]) / RMT_PERIOD_NS;
	        if ((t1_len != 2) || (t0_len != 2)) {
	        	mp_raise_ValueError("Wrong arguments");
	        }
        }
        else {
        	mp_raise_ValueError("tuple argument expected");
        }
        pixel_timing_t tm;
        if (rst > 0) {
			tm.mark.level1 = 1;
			tm.mark.duration1 = (mp_obj_get_int(t1_items[0]) & 0x7FFF) / RMT_PERIOD_NS;
			tm.mark.level0 = 0;
			tm.mark.duration0 = (mp_obj_get_int(t1_items[1]) & 0x7FFF) / RMT_PERIOD_NS;

			tm.space.level1 = 1;
			tm.space.duration1 = (mp_obj_get_int(t0_items[0]) & 0x7FFF) / RMT_PERIOD_NS;
			tm.space.level0 = 0;
			tm.space.duration0 = (mp_obj_get_int(t0_items[1]) & 0x7FFF) / RMT_PERIOD_NS;

			tm.reset.level1 = 0;
			tm.reset.duration1 = rst / 2;
			tm.reset.level0 = 0;
			tm.reset.duration0 = rst / 2;
        }
        memcpy(&self->px.timings, &tm, sizeof(pixel_timing_t));
    }

    mp_obj_t t1_tuple[2];
   	mp_obj_t t0_tuple[2];
   	mp_obj_t t_tuple[3];

   	t1_tuple[0] = mp_obj_new_int(self->px.timings.mark.duration0 * RMT_PERIOD_NS);
   	t1_tuple[1] = mp_obj_new_int(self->px.timings.mark.duration1 * RMT_PERIOD_NS);

   	t0_tuple[0] = mp_obj_new_int(self->px.timings.space.duration0 * RMT_PERIOD_NS);
   	t0_tuple[1] = mp_obj_new_int(self->px.timings.space.duration1 * RMT_PERIOD_NS);

   	t_tuple[0] = mp_obj_new_tuple(2, t1_tuple);
   	t_tuple[1] = mp_obj_new_tuple(2, t0_tuple);
   	t_tuple[2] = mp_obj_new_int((self->px.timings.reset.duration0 + self->px.timings.reset.duration1) * RMT_PERIOD_NS);

   	return mp_obj_new_tuple(3, t_tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_neopixel_timings_obj, 0, machine_neopixel_timings);

//-------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_neopixel_rainbow(mp_obj_t self_in, mp_obj_t pos_in, mp_obj_t fact_in)
{
    machine_neopixel_obj_t *self = self_in;
    np_check(self);

    uint32_t color;
    int pos = mp_obj_get_int(pos_in);
    int fact = mp_obj_get_int(fact_in);
    float hue;
    float dHue = 360.0 * fact / (float)self->px.pixel_count;
    float bri = (float)self->px.brightness / 255.0;

    for (int i=0; i<self->px.pixel_count; i++) {
        hue = fmod((dHue * (pos+i)), 360.0);
        color = hsb_to_rgb(hue, 1.0, bri) << 8;
		np_set_pixel_color(&self->px, i, color);
    }
   	MP_THREAD_GIL_EXIT();
	np_show(&self->px, self->channel);
   	MP_THREAD_GIL_ENTER();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(machine_neopixel_rainbow_obj, machine_neopixel_rainbow);

//-----------------------------------------------------
STATIC mp_obj_t machine_neopixel_info(mp_obj_t self_in)
{
    machine_neopixel_obj_t *self = self_in;
    np_check(self);

    char colorder[5];
    sprintf(colorder, self->px.color_order);
    if (self->px.nbits == 24) colorder[3] = '\0';

    mp_obj_t tuple[6];

	tuple[0] = mp_obj_new_int(self->gpio_num);
	tuple[1] = mp_obj_new_int(self->px.pixel_count);
	tuple[2] = mp_obj_new_int(self->px.nbits);
	tuple[3] = mp_obj_new_int(self->channel);
	tuple[4] = mp_obj_new_int(sizeof(uint32_t) * self->px.pixel_count);
	tuple[5] = mp_obj_new_str(colorder, strlen(colorder));

	return mp_obj_new_tuple(6, tuple);

}
MP_DEFINE_CONST_FUN_OBJ_1(machine_neopixel_info_obj, machine_neopixel_info);


//=====================================================================
STATIC const mp_rom_map_elem_t machine_neopixel_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_clear),      (mp_obj_t)&machine_neopixel_clear_obj },
    { MP_ROM_QSTR(MP_QSTR_set),        (mp_obj_t)&machine_neopixel_set_obj },
    { MP_ROM_QSTR(MP_QSTR_setWhite),   (mp_obj_t)&machine_neopixel_set_white_obj },
    { MP_ROM_QSTR(MP_QSTR_setHSB),     (mp_obj_t)&machine_neopixel_setHSB_obj },
    { MP_ROM_QSTR(MP_QSTR_setHSBint),  (mp_obj_t)&machine_neopixel_setHSB_int_obj },
    { MP_ROM_QSTR(MP_QSTR_get),        (mp_obj_t)&machine_neopixel_get_obj },
    { MP_ROM_QSTR(MP_QSTR_show),       (mp_obj_t)&machine_neopixel_show_obj },
    { MP_ROM_QSTR(MP_QSTR_brightness), (mp_obj_t)&machine_neopixel_brightness_obj },
    { MP_ROM_QSTR(MP_QSTR_HSBtoRGB),   (mp_obj_t)&machine_neopixel_HSBtoRGB_obj },
    { MP_ROM_QSTR(MP_QSTR_HSBtoRGBint),(mp_obj_t)&machine_neopixel_HSBtoRGBint_obj },
    { MP_ROM_QSTR(MP_QSTR_RGBtoHSB),   (mp_obj_t)&machine_neopixel_RGBtoHSB_obj },
    { MP_ROM_QSTR(MP_QSTR_deinit),     (mp_obj_t)&machine_neopixel_deinit_obj },
    { MP_ROM_QSTR(MP_QSTR_timings),    (mp_obj_t)&machine_neopixel_timings_obj },
    { MP_ROM_QSTR(MP_QSTR_color_order),(mp_obj_t)&machine_neopixel_corder_obj },
    { MP_ROM_QSTR(MP_QSTR_rainbow),    (mp_obj_t)&machine_neopixel_rainbow_obj },
    { MP_ROM_QSTR(MP_QSTR_info),	   (mp_obj_t)&machine_neopixel_info_obj },

	{ MP_ROM_QSTR(MP_QSTR_BLACK),		MP_ROM_INT(0x000000) },
	{ MP_ROM_QSTR(MP_QSTR_WHITE),		MP_ROM_INT(0xFFFFFF) },
	{ MP_ROM_QSTR(MP_QSTR_RED),			MP_ROM_INT(0xFF0000) },
	{ MP_ROM_QSTR(MP_QSTR_LIME),		MP_ROM_INT(0x00FF00) },
	{ MP_ROM_QSTR(MP_QSTR_BLUE),		MP_ROM_INT(0x0000FF) },
	{ MP_ROM_QSTR(MP_QSTR_YELLOW),		MP_ROM_INT(0xFFFF00) },
	{ MP_ROM_QSTR(MP_QSTR_CYAN),		MP_ROM_INT(0x00FFFF) },
	{ MP_ROM_QSTR(MP_QSTR_MAGENTA),		MP_ROM_INT(0xFF00FF) },
	{ MP_ROM_QSTR(MP_QSTR_SILVER),		MP_ROM_INT(0xC0C0C0) },
	{ MP_ROM_QSTR(MP_QSTR_GRAY),		MP_ROM_INT(0x808080) },
	{ MP_ROM_QSTR(MP_QSTR_MAROON),		MP_ROM_INT(0x800000) },
	{ MP_ROM_QSTR(MP_QSTR_OLIVE),		MP_ROM_INT(0x808000) },
	{ MP_ROM_QSTR(MP_QSTR_GREEN),		MP_ROM_INT(0x008000) },
	{ MP_ROM_QSTR(MP_QSTR_PURPLE),		MP_ROM_INT(0x800080) },
	{ MP_ROM_QSTR(MP_QSTR_TEAL),		MP_ROM_INT(0x008080) },
	{ MP_ROM_QSTR(MP_QSTR_NAVY),		MP_ROM_INT(0x000080) },

	{ MP_ROM_QSTR(MP_QSTR_TYPE_RGB),	MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_TYPE_RGBW),	MP_ROM_INT(1) },
};

STATIC MP_DEFINE_CONST_DICT(machine_neopixel_locals_dict, machine_neopixel_locals_dict_table);

//===========================================
const mp_obj_type_t machine_neopixel_type = {
    { &mp_type_type },
    .name = MP_QSTR_Neopixel,
    .print = machine_neopixel_print,
    .make_new = machine_neopixel_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_neopixel_locals_dict,
};

