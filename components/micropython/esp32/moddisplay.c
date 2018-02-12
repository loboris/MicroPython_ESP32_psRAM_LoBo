/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2015 Damien P. George
 * Copyright (c) 2016 Paul Sokolovsky
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

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_DISPLAY

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"
#include "esp_system.h"

#include "py/obj.h"
#include "py/objint.h"
#include "py/runtime.h"

#include "tft/spi_master_lobo.h"
#include "driver/gpio.h"
#include "tft/tftspi.h"
#include "tft/tft.h"
#include "extmod/vfs_native.h"


typedef struct _display_tft_obj_t {
    mp_obj_base_t base;
    int type;
    spi_lobo_device_handle_t spi;
    spi_lobo_device_handle_t tsspi;
    uint32_t spi_speed;
    uint32_t spi_rdspeed;
    int width;
    int height;
    uint8_t miso;
    uint8_t mosi;
    uint8_t clk;
    uint8_t cs;
    uint8_t dc;
    int8_t tcs;
    int8_t rst;
    int8_t bckl;
    uint8_t bckl_on;
    uint8_t invrot;
    uint8_t bgr;
    uint8_t tp_type;
    uint32_t tp_calx;
    uint32_t tp_caly;
} display_tft_obj_t;

extern int checkSPIBUS(int8_t bus, int8_t mosi, uint8_t miso, uint8_t sck);

STATIC display_tft_obj_t display_tft_obj;
const mp_obj_type_t display_tft_type;

uint8_t disp_used_spi_host = 0;

// constructor(id, ...)
//-----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {

    display_tft_obj_t *self = (display_tft_obj_t*)&display_tft_obj;
    self->base.type = &display_tft_type;
    self->type = -1;
    self->spi = NULL;
    self->tsspi = NULL;
    self->spi_speed = 0;

    // return constant object
    return (mp_obj_t)&display_tft_obj;
}

//-----------------------------------------------------------------------------------------------
STATIC void display_tft_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
	display_tft_obj_t *self = self_in;
    if ((self->spi) && (self->type >= 0)) {
    	char stype[16];
    	if (self->type == DISP_TYPE_ILI9341) sprintf(stype, "ILI9341");
    	else if (self->type == DISP_TYPE_ILI9488) sprintf(stype, "ILI9488");
    	else if (self->type == DISP_TYPE_ST7789V) sprintf(stype, "ST7789V");

    	mp_printf(print, "TFT  (%dx%d, Type=%s, Ready: %s, Clk=%u Hz, RdClk=%u Hz, Touch: %s)\n",
    			self->width, self->height, stype, ((self->spi) ? "yes" : "no"), self->spi_speed, self->spi_rdspeed, ((self->tsspi) ? "yes" : "no"));
    	mp_printf(print, "Pins (miso=%d, mosi=%d, clk=%d, cs=%d, dc=%d, reset=%d, backlight=%d)", self->miso, self->mosi, self->clk, self->cs, self->dc, self->rst, self->bckl);
    	if (self->tsspi) {
    		mp_printf(print, "\nTouch(Enabled, cs=%d)", self->tcs);
    	}
    }
    else {
    	mp_printf(print, "TFT (Not initialized)");
    }
}

//-------------------------------------------------
static int setupDevice(display_tft_obj_t *disp_dev)
{
    if (disp_dev->spi == NULL) return 1;
    tft_disp_type = disp_dev->type;
    pin_dc = disp_dev->dc;
    pin_rst = disp_dev->rst;
    pin_bckl = disp_dev->bckl;
    bckl_on = disp_dev->bckl_on;
	disp_spi = disp_dev->spi;
	ts_spi = disp_dev->tsspi;
	_width = disp_dev->width;
	_height = disp_dev->height;
	_invert_rot = disp_dev->invrot;
	_rgb_bgr = disp_dev->bgr;
	max_rdclock = disp_dev->spi_rdspeed;
	tp_type = disp_dev->tp_type;
	tp_calx = disp_dev->tp_calx;
	tp_caly = disp_dev->tp_caly;
	return 0;
}

//--------------------------------------
STATIC color_t intToColor(uint32_t cint)
{
	color_t cl = {0,0,0};
	cl.r = (cint >> 16) & 0xFF;
	cl.g = (cint >> 8) & 0xFF;
	cl.b = cint & 0xFF;
	return cl;
}

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_init(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
	enum { ARG_type, ARG_host, ARG_width, ARG_height, ARG_speed, ARG_miso, ARG_mosi, ARG_clk,
		ARG_cs, ARG_dc, ARG_tcs, ARG_rst, ARG_bckl, ARG_bcklon, ARG_hastouch, ARG_invrot, ARG_bgr, ARG_rot, ARG_splash };
    const mp_arg_t allowed_args[] = {
		{ MP_QSTR_type,      MP_ARG_REQUIRED                   | MP_ARG_INT,  { .u_int = DISP_TYPE_ST7789V } },
		{ MP_QSTR_spihost,                     MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = LOBO_HSPI_HOST } },
		{ MP_QSTR_width,                       MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = DEFAULT_TFT_DISPLAY_WIDTH } },
		{ MP_QSTR_height,                      MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = DEFAULT_TFT_DISPLAY_HEIGHT } },
		{ MP_QSTR_speed,                       MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = DEFAULT_SPI_CLOCK } },
        { MP_QSTR_miso,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = PIN_NUM_MISO } },
        { MP_QSTR_mosi,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = PIN_NUM_MOSI } },
        { MP_QSTR_clk,       MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = PIN_NUM_CLK } },
        { MP_QSTR_cs,        MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = PIN_NUM_CS } },
        { MP_QSTR_dc,        MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = PIN_NUM_DC } },
        { MP_QSTR_tcs,                         MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_rst_pin,                     MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_backl_pin,                   MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_backl_on,                    MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_hastouch,                    MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_invrot,                      MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_bgr,                         MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = false } },
        { MP_QSTR_rot,                         MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = PORTRAIT } },
        { MP_QSTR_splash,                      MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = true } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_tft_obj_t *self = pos_args[0];
    esp_err_t ret;

    if (self->spi) {
    	// === deinitialize spi device(s) if it was initialized ===
    	if (self->tsspi) {
    		ret = spi_lobo_bus_remove_device(self->tsspi);
    		self->tsspi = NULL;
    		ts_spi = NULL;
    	}
		ret = spi_lobo_bus_remove_device(self->spi);
		self->spi = NULL;
		disp_spi = NULL;
		self->type = -1;
	    self->spi_speed = 0;
    }

    // === Get arguments ===
	#if CONFIG_SPIRAM_SPEED_80M
    if (args[ARG_host].u_int != LOBO_HSPI_HOST) {
        mp_raise_msg(&mp_type_OSError, "Unsupported SPI host");
    }
	#else
    if ((args[ARG_host].u_int != LOBO_HSPI_HOST) && (args[ARG_host].u_int != LOBO_VSPI_HOST)) {
        mp_raise_msg(&mp_type_OSError, "Unsupported SPI host");
    }
	#endif

    int bus_state = checkSPIBUS(args[ARG_host].u_int, args[ARG_mosi].u_int, args[ARG_miso].u_int, args[ARG_clk].u_int);
    if (bus_state != 1) {
        mp_raise_ValueError("SPI host already used by SPI module with different configuration");
    }

    if ((args[ARG_type].u_int < 0) || (args[ARG_type].u_int >= DISP_TYPE_MAX)) {
        mp_raise_msg(&mp_type_OSError, "Unsupported display type");
	}

    self->type = args[ARG_type].u_int;
    self->tp_type = TOUCH_TYPE_NONE;

    self->width = args[ARG_width].u_int;   // smaller dimension
	self->height = args[ARG_height].u_int; // larger dimension
	self->spi_rdspeed = 8000000;
    if (args[ARG_invrot].u_int >= 0) self->invrot = args[ARG_invrot].u_int;
    else {
    	if ((self->type == DISP_TYPE_ST7789V) ||
    			(self->type == DISP_TYPE_ST7735) ||
				(self->type == DISP_TYPE_ST7735R) ||
				(self->type == DISP_TYPE_ST7735B)) self->invrot = 2;
    	else self->invrot = 0;
    }

    if (args[ARG_bgr].u_bool) self->bgr = 8;
    else self->bgr = 0;

    self->rst = args[ARG_rst].u_int;
    self->bckl = args[ARG_bckl].u_int;
    self->bckl_on = args[ARG_bcklon].u_int & 1;

    self->miso = args[ARG_miso].u_int;
    self->mosi = args[ARG_mosi].u_int;
    self->clk = args[ARG_clk].u_int;

    self->cs = args[ARG_cs].u_int;
	self->dc = args[ARG_dc].u_int;

	self->tcs = args[ARG_tcs].u_int;

	// === Configure pins ===
    // Route all used pins to GPIO control (some pins may initially be routed to other devices)
    gpio_pad_select_gpio(self->cs);
    gpio_pad_select_gpio(self->miso);
    gpio_pad_select_gpio(self->mosi);
    gpio_pad_select_gpio(self->clk);
    gpio_pad_select_gpio(self->dc);
    if (self->tcs >= 0) {
    	gpio_pad_select_gpio(self->tcs);
        gpio_set_direction(self->tcs, GPIO_MODE_OUTPUT);
        gpio_set_level(self->tcs, 1);
    }
    if (self->rst >= 0) {
    	gpio_pad_select_gpio(self->rst);
        gpio_set_direction(self->rst, GPIO_MODE_OUTPUT);
        gpio_set_level(self->rst, 0);
    }
    if (self->bckl >= 0) {
    	gpio_pad_select_gpio(self->bckl);
        gpio_set_direction(self->bckl, GPIO_MODE_OUTPUT);
        gpio_set_level(self->bckl, (self->bckl_on & 1) ^ 1);
    }

    gpio_set_direction(self->miso, GPIO_MODE_INPUT);
    gpio_set_pull_mode(self->miso, GPIO_PULLUP_ONLY);
    gpio_set_direction(self->cs, GPIO_MODE_OUTPUT);
    gpio_set_direction(self->mosi, GPIO_MODE_OUTPUT);
    gpio_set_direction(self->clk, GPIO_MODE_OUTPUT);
    gpio_set_direction(self->dc, GPIO_MODE_OUTPUT);
    gpio_set_level(self->dc, 0);
    gpio_set_level(self->cs, 1);

    // ====  CONFIGURE SPI DEVICES(s)  ====================================================================================

    spi_lobo_bus_config_t buscfg = {
        .miso_io_num = self->miso,	// set SPI MISO pin
        .mosi_io_num = self->mosi,	// set SPI MOSI pin
        .sclk_io_num = self->clk,	// set SPI CLK pin
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
		.max_transfer_sz = 6*1024,
    };
    spi_lobo_device_interface_config_t devcfg = {
        .clock_speed_hz = 8000000,          // Initial spi clock at 8 MHz
        .mode = 0,                          // SPI mode 0
        .spics_io_num = -1,              	// we will use external CS pin
		.spics_ext_io_num = self->cs,		// external CS pin
		.flags = LB_SPI_DEVICE_HALFDUPLEX,  // ALWAYS SET to HALF DUPLEX MODE!! for display spi
    };

    // ====================================================================================================================

    // ==================================================================
	// ==== Initialize the SPI bus and attach the LCD to the SPI bus ====

	ret=spi_lobo_bus_add_device(args[ARG_host].u_int, &buscfg, &devcfg, &self->spi);
	if (ret != ESP_OK) {
		self->spi = NULL;
        mp_raise_msg(&mp_type_OSError, "error adding spi device");
	}

	// ==== Test display select/deselect ====
	ret = spi_lobo_device_select(self->spi, 1);
	if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "error selecting spi display device");
	}
	ret = spi_lobo_device_deselect(self->spi);

	ts_spi = NULL;
	self->tsspi = NULL;
    if ((args[ARG_hastouch].u_int == TOUCH_TYPE_XPT2046) || (args[ARG_hastouch].u_int == TOUCH_TYPE_STMPE610)) {
    	if (self->tcs >= 0) {
			// === If touch is used, add it to the bus ===
			self->tp_calx = TP_CALX_XPT2046;
			self->tp_caly = TP_CALY_XPT2046;
			self->tp_type = TOUCH_TYPE_XPT2046;
			spi_lobo_device_interface_config_t tsdevcfg = {
					.clock_speed_hz = 2500000,    // Clock out at 2.5 MHz
					.mode = 0,                    // SPI mode 0
					.spics_io_num = self->tcs,    // Touch CS pin
					.spics_ext_io_num = -1,       // Not using the external CS
					//.command_bits = 8,            // 1 byte command
				};
			if (args[ARG_hastouch].u_int == TOUCH_TYPE_STMPE610) {
				tsdevcfg.clock_speed_hz = 1000000;  // Clock out at 1 MHz
				tsdevcfg.mode = 1;                  // SPI mode 1
				self->tp_type = TOUCH_TYPE_STMPE610;
				self->tp_calx = TP_CALX_STMPE610;
				self->tp_caly = TP_CALY_STMPE610;
			}
			ret=spi_lobo_bus_add_device(args[ARG_host].u_int, &buscfg, &tsdevcfg, &self->tsspi);
			if (ret != ESP_OK) {
				ts_spi = NULL;
				self->tsspi = NULL;
				self->tp_type = TOUCH_TYPE_NONE;
			}

			// ==== Test select/deselect ====
			ret = spi_lobo_device_select(self->tsspi, 1);
			if (ret != ESP_OK) {
				ts_spi = NULL;
				self->tsspi = NULL;
				self->tp_type = TOUCH_TYPE_NONE;
			}
			spi_lobo_device_deselect(self->tsspi);
    	}
    	else {
			mp_printf(&mp_plat_print, "Touch panel CS pin not specified, touch not enabled!\n");
    	}
    }
    disp_used_spi_host = args[ARG_host].u_int;

    // ================================
	// ==== Initialize the Display ====

    setupDevice(self);
	TFT_display_init();
	if (self->tp_type == TOUCH_TYPE_STMPE610) {
		stmpe610_Init();
	}

	// ==== Set SPI clock used for display operations ====
	self->spi_speed = spi_lobo_set_speed(self->spi, args[ARG_speed].u_int);

	max_rdclock = find_rd_speed();
	self->spi_rdspeed = max_rdclock;

	font_rotate = 0;
	text_wrap = 0;
	font_transparent = 0;
	font_forceFixed = 0;
	gray_scale = 0;
	TFT_setRotation(args[ARG_rot].u_int & 3);
    TFT_setGammaCurve(DEFAULT_GAMMA_CURVE);
	TFT_setFont(DEFAULT_FONT, NULL);
	TFT_resetclipwin();
	if (args[ARG_splash].u_bool) {
		int fhight = TFT_getfontheight();
		_fg = intToColor(iTFT_RED);
		TFT_print("MicroPython", CENTER, (_height/2) - fhight - (fhight/2));
		_fg = intToColor(iTFT_GREEN);
		TFT_print("MicroPython", CENTER, (_height/2) - (fhight/2));
		_fg = intToColor(iTFT_BLUE);
		TFT_print("MicroPython", CENTER, (_height/2) + (fhight/2));
		_fg = intToColor(iTFT_GREEN);
	}
	bcklOn();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_init_obj, 0, display_tft_init);

//-------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawPixel(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_color,               MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    if (args[2].u_int >= 0) {
    	color = intToColor(args[2].u_int);
    }
    TFT_drawPixel(x, y, color, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawPixel_obj, 2, display_tft_drawPixel);

//-------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_readPixel(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;

    color_t color = TFT_readPixel(x, y);
    mp_int_t icolor = (int)((color.r << 16) | (color.g << 8) | color.b);

    return mp_obj_new_int(icolor);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_readPixel_obj, 2, display_tft_readPixel);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawLine(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_x1,    MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y1,    MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_color,                   MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x0 = args[0].u_int;
    mp_int_t y0 = args[1].u_int;
	mp_int_t x1 = args[2].u_int;
    mp_int_t y1 = args[3].u_int;
    if (args[4].u_int >= 0) {
    	color = intToColor(args[4].u_int);
    }
    TFT_drawLine(x0, y0, x1, y1, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawLine_obj, 4, display_tft_drawLine);

//-------------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawLineByAngle(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_start,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_length, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_angle,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
	mp_int_t start = args[2].u_int;
    mp_int_t len = args[3].u_int;
    mp_int_t angle = args[4].u_int;
    if (args[5].u_int >= 0) {
    	color = intToColor(args[5].u_int);
    }
    TFT_drawLineByAngle(x, y, start, len, angle, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawLineByAngle_obj, 5, display_tft_drawLineByAngle);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawTriangle(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_x1,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y1,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_x2,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y2,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x0 = args[0].u_int;
    mp_int_t y0 = args[1].u_int;
	mp_int_t x1 = args[2].u_int;
    mp_int_t y1 = args[3].u_int;
	mp_int_t x2 = args[4].u_int;
    mp_int_t y2 = args[5].u_int;
    if (args[6].u_int >= 0) {
    	color = intToColor(args[6].u_int);
    }
    if (args[7].u_int >= 0) {
        TFT_fillTriangle(x0, y0, x1, y1, x2, y2, intToColor(args[7].u_int));
    }
    TFT_drawTriangle(x0, y0, x1, y1, x2, y2, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawTriangle_obj, 6, display_tft_drawTriangle);

//--------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawCircle(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_r,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    mp_int_t radius = args[2].u_int;
    if (args[3].u_int >= 0) {
    	color = intToColor(args[3].u_int);
    }
    if (args[4].u_int >= 0) {
        TFT_fillCircle(x, y, radius, intToColor(args[4].u_int));
        if (args[3].u_int != args[4].u_int) TFT_drawCircle(x, y, radius, color);
    }
    else TFT_drawCircle(x, y, radius, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawCircle_obj, 3, display_tft_drawCircle);

//---------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawEllipse(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_rx,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_ry,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_opt,                      MP_ARG_INT, { .u_int = 15 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
	mp_int_t rx = args[2].u_int;
    mp_int_t ry = args[3].u_int;
    mp_int_t opt = args[4].u_int & 0x0F;
    if (args[5].u_int >= 0) {
    	color = intToColor(args[5].u_int);
    }
    if (args[6].u_int >= 0) {
        TFT_fillEllipse(x, y, rx, ry, intToColor(args[6].u_int), opt);
    }
    TFT_drawEllipse(x, y, rx, ry, color, opt);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawEllipse_obj, 4, display_tft_drawEllipse);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawArc(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_r,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_thick,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_start,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_end,    MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 15 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
    color_t fill_color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    mp_int_t r = args[2].u_int;
	mp_int_t th = args[3].u_int;
    mp_int_t start = args[4].u_int;
    mp_int_t end = args[5].u_int;
    if (args[6].u_int >= 0) {
    	color = intToColor(args[6].u_int);
    }
    if (args[7].u_int >= 0) {
    	fill_color = intToColor(args[7].u_int);
    }
    TFT_drawArc(x, y, r, th, start, end, color, fill_color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawArc_obj, 6, display_tft_drawArc);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawPoly(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_r,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_sides,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_thick,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 1 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_rotate,                   MP_ARG_INT, { .u_int = 0 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
    color_t fill_color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    mp_int_t r = args[2].u_int;
	mp_int_t sides = args[3].u_int;
	mp_int_t th = args[4].u_int;
    if (args[5].u_int >= 0) {
    	color = intToColor(args[5].u_int);
    }
    if (args[6].u_int >= 0) {
    	fill_color = intToColor(args[6].u_int);
    }
    TFT_drawPolygon(x, y, sides, r, color, fill_color, args[7].u_int, th);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawPoly_obj, 5, display_tft_drawPoly);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawRect(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
	mp_int_t w = args[2].u_int;
    mp_int_t h = args[3].u_int;
    if (args[4].u_int >= 0) {
    	color = intToColor(args[4].u_int);
    }
    if (args[5].u_int >= 0) {
        TFT_fillRect(x, y, w, h, intToColor(args[5].u_int));
    }
    TFT_drawRect(x, y, w, h, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawRect_obj, 4, display_tft_drawRect);

//-----------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_drawRoundRect(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_r,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
	mp_int_t w = args[2].u_int;
    mp_int_t h = args[3].u_int;
    mp_int_t r = args[4].u_int;
    if (args[5].u_int >= 0) {
    	color = intToColor(args[5].u_int);
    }
    if (args[6].u_int >= 0) {
        TFT_fillRoundRect(x, y, w, h, r, intToColor(args[6].u_int));
    }
    TFT_drawRoundRect(x, y, w, h, r, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawRoundRect_obj, 5, display_tft_drawRoundRect);

//--------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_fillScreen(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _bg;
    if (args[0].u_int >= 0) {
    	color = intToColor(args[0].u_int);
    }
    TFT_fillScreen(color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_fillScreen_obj, 0, display_tft_fillScreen);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_fillWin(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_color,                    MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t color = _bg;
    if (args[0].u_int >= 0) {
    	color = intToColor(args[0].u_int);
    }
    TFT_fillWindow(color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_fillWin_obj, 0, display_tft_fillWin);

//--------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_7segAttrib(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_dist,    MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,   MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_outline, MP_ARG_REQUIRED | MP_ARG_BOOL, { .u_bool = false } },
        { MP_QSTR_color,   MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    set_7seg_font_atrib(args[0].u_int, args[1].u_int, (int)args[2].u_bool, intToColor(args[3].u_int));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_7segAttrib_obj, 4, display_tft_7segAttrib);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_setFont(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_font,         MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_rotate,       MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_transparent,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fixedwidth,   MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_dist,         MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = 8 } },
        { MP_QSTR_width,        MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = 2 } },
        { MP_QSTR_outline,      MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = false } },
        { MP_QSTR_color,        MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = 0 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *font_file = NULL;
    char fullname[128] = {'\0'};
    mp_int_t font = DEFAULT_FONT;

    if (MP_OBJ_IS_STR(args[0].u_obj)) {
        font_file = mp_obj_str_get_str(args[0].u_obj);

        if (physicalPath(font_file, fullname) == 0) {
			font = USER_FONT;
			font_file = fullname;
        }
    }
    else {
    	font = mp_obj_get_int(args[0].u_obj);
    }
    TFT_setFont(font, font_file);

    if (args[1].u_int >= 0) font_rotate = args[1].u_int;
    if (args[2].u_int >= 0) font_transparent = args[2].u_int;
    if (args[3].u_int >= 0) font_forceFixed = args[3].u_int;

    if (font == FONT_7SEG) {
        set_7seg_font_atrib(args[4].u_int, args[5].u_int, (int)args[6].u_bool, intToColor(args[7].u_int));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_setFont_obj, 1, display_tft_setFont);

//-------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_getFontSize(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    int width, height;
    TFT_getfontsize(&width, &height);

    mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int(width);
    tuple[1] = mp_obj_new_int(height);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_getFontSize_obj, 0, display_tft_getFontSize);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_setRot(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_rot, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = PORTRAIT } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

  	mp_int_t rot = args[0].u_int;
  	if ((rot < 0) || (rot > 3)) rot = 0;

  	TFT_setRotation(rot);
  	self->width = _width;
  	self->height = _height;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_setRot_obj, 1, display_tft_setRot);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_print(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_text,         MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_color,                          MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_rotate,       MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_transparent,  MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_fixedwidth,   MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_wrap,         MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t old_fg = _fg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    char *st = (char *)mp_obj_str_get_str(args[2].u_obj);
    if (args[3].u_int >= 0) {
    	_fg = intToColor(args[3].u_int);
    }
    if (args[4].u_int >= 0) font_rotate = args[4].u_int;
    if (mp_obj_is_integer(args[5].u_obj)) font_transparent = args[5].u_int;
    if (mp_obj_is_integer(args[6].u_obj)) font_forceFixed = args[6].u_int;
    if (mp_obj_is_integer(args[7].u_obj)) text_wrap = args[7].u_int;

    TFT_print(st, x, y);
    _fg = old_fg;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_print_obj, 3, display_tft_print);

//---------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_stringWidth(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_text,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *st = (char *)mp_obj_str_get_str(args[0].u_obj);

    mp_int_t w = TFT_getStringWidth(st);

    return mp_obj_new_int(w);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_stringWidth_obj, 1, display_tft_stringWidth);

//-------------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_clearStringRect(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,       MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,       MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_text,    MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_color,                     MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t old_bg = _bg;
	mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    char *st = (char *)mp_obj_str_get_str(args[2].u_obj);
    if (args[3].u_int >= 0) {
    	_fg = intToColor(args[3].u_int);
    }

    TFT_clearStringRect(x, y, st);
    _bg = old_bg;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_clearStringRect_obj, 3, display_tft_clearStringRect);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_Image(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
		{ MP_QSTR_x,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
		{ MP_QSTR_y,     MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_file,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_scale,                   MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_type,                    MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_debug, MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = 0 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *fname = NULL;
    char fullname[128] = {'\0'};
    int img_type = args[4].u_int;

    fname = (char *)mp_obj_str_get_str(args[2].u_obj);

    int res = physicalPath(fname, fullname);
    if ((res != 0) || (strlen(fullname) == 0)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
    }

    if (img_type < 0) {
    	// try to determine image type
        char upr_fname[128];
        strcpy(upr_fname, fname);
        for (int i=0; i < strlen(upr_fname); i++) {
          upr_fname[i] = toupper((unsigned char) upr_fname[i]);
        }
        if (strstr(upr_fname, ".JPG") != NULL) img_type = IMAGE_TYPE_JPG;
        else if (strstr(upr_fname, ".BMP") != NULL) img_type = IMAGE_TYPE_BMP;
        else {
        	FILE *fhndl = fopen(fullname, "r");
            if (fhndl != NULL) {
            	uint8_t buf[16];
            	if (fread(buf, 1, 11, fhndl) == 11) {
            		buf[10] = 0;
            		if (strstr((char *)(buf+6), "JFIF") != NULL) img_type = IMAGE_TYPE_JPG;
            		else if ((buf[0] = 0x42) && (buf[1] = 0x4d)) img_type = IMAGE_TYPE_BMP;
            	}
            	fclose(fhndl);
            }
        }
        if (img_type < 0) {
        	nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Cannot determine image type"));
        }
    }

    image_debug = (uint8_t)args[5].u_bool;
    if (img_type == IMAGE_TYPE_BMP) {
    	TFT_bmp_image(args[0].u_int, args[1].u_int, args[3].u_int, fullname, NULL, 0);
    }
    else if (img_type == IMAGE_TYPE_JPG) {
    	TFT_jpg_image(args[0].u_int, args[1].u_int, args[3].u_int, fullname, NULL, 0);
    }
    else {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Unsupported image type"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_Image_obj, 3, display_tft_Image);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_getTouch(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_raw, MP_ARG_BOOL, { .u_bool = false } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;
    if (self->tsspi == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Touch not configured"));
    }

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int x = 0;
    int y = 0;
    uint8_t raw = 0;
    if (args[0].u_bool) raw = 1;

    int res = TFT_read_touch(&x, &y, raw);

    mp_obj_t tuple[3];
    tuple[0] = mp_obj_new_bool(res);
    tuple[1] = mp_obj_new_int(x);
    tuple[2] = mp_obj_new_int(y);

    return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_getTouch_obj, 0, display_tft_getTouch);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_compileFont(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_file,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_debug, MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = false } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    //if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *fname = NULL;
    char fullname[128] = {'\0'};
    uint8_t debug = (uint8_t)args[1].u_bool;

    fname = (char *)mp_obj_str_get_str(args[0].u_obj);

    int res = physicalPath(fname, fullname);
    if ((res != 0) || (strlen(fullname) == 0)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
    }

    res = compile_font_file(fullname, debug);
    if (res) return mp_const_false;
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_compileFont_obj, 1, display_tft_compileFont);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_HSBtoRGB(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_hue,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_saturation,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_brightness,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_float_t hue = mp_obj_get_float(args[0].u_obj);
    mp_float_t sat = mp_obj_get_float(args[1].u_obj);
    mp_float_t bri = mp_obj_get_float(args[2].u_obj);

    color_t color = HSBtoRGB(hue, sat, bri);
    mp_int_t icolor = (int)((color.r << 16) | (color.g << 8) | color.b);

    return mp_obj_new_int(icolor);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_HSBtoRGB_obj, 3, display_tft_HSBtoRGB);

//--------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_setclipwin(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_x1, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y1, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_fillcolor,                MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

	mp_int_t x0 = args[0].u_int;
    mp_int_t y0 = args[1].u_int;
	mp_int_t x1 = args[2].u_int;
    mp_int_t y1 = args[3].u_int;

    TFT_setclipwin(x0, y0, x1, y1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_setclipwin_obj, 4, display_tft_setclipwin);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_resetclipwin(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    TFT_resetclipwin();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_resetclipwin_obj, 0, display_tft_resetclipwin);

//---------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_saveclipwin(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    TFT_saveClipWin();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_saveclipwin_obj, 0, display_tft_saveclipwin);

//------------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_restoreclipwin(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    TFT_restoreClipWin();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_restoreclipwin_obj, 0, display_tft_restoreclipwin);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_getSize(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(_width);
    tuple[1] = mp_obj_new_int(_height);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_getSize_obj, 0, display_tft_getSize);

//--------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_getWinSize(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(dispWin.x2 - dispWin.x1 + 1);
    tuple[1] = mp_obj_new_int(dispWin.y2 - dispWin.y1 + 1);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_getWinSize_obj, 0, display_tft_getWinSize);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_setCalib(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_calx, MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_caly, MP_ARG_INT, { .u_int = 0 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (self->tp_type == TOUCH_TYPE_NONE) {
        return mp_const_none;
    }

    if (args[0].u_int == 0) {
		if (self->tp_type == TOUCH_TYPE_XPT2046) self->tp_calx = TP_CALX_XPT2046;
		else self->tp_calx = TP_CALX_STMPE610;
    }
    else self->tp_calx = args[0].u_int;

    if (args[0].u_int == 0) {
		if (self->tp_type == TOUCH_TYPE_XPT2046) self->tp_caly = TP_CALY_XPT2046;
		else self->tp_caly = TP_CALY_STMPE610;
    }
    else self->tp_caly = args[1].u_int;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_setCalib_obj, 0, display_tft_setCalib);



//================================================================
STATIC const mp_rom_map_elem_t display_tft_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&display_tft_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&display_tft_drawPixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_readPixel), MP_ROM_PTR(&display_tft_readPixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&display_tft_drawLine_obj) },
    { MP_ROM_QSTR(MP_QSTR_lineByAngle), MP_ROM_PTR(&display_tft_drawLineByAngle_obj) },
    { MP_ROM_QSTR(MP_QSTR_triangle), MP_ROM_PTR(&display_tft_drawTriangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&display_tft_drawCircle_obj) },
    { MP_ROM_QSTR(MP_QSTR_ellipse), MP_ROM_PTR(&display_tft_drawEllipse_obj) },
    { MP_ROM_QSTR(MP_QSTR_arc), MP_ROM_PTR(&display_tft_drawArc_obj) },
    { MP_ROM_QSTR(MP_QSTR_polygon), MP_ROM_PTR(&display_tft_drawPoly_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&display_tft_drawRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_roundrect), MP_ROM_PTR(&display_tft_drawRoundRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&display_tft_fillScreen_obj) },
    { MP_ROM_QSTR(MP_QSTR_clearwin), MP_ROM_PTR(&display_tft_fillWin_obj) },
    { MP_ROM_QSTR(MP_QSTR_font), MP_ROM_PTR(&display_tft_setFont_obj) },
    { MP_ROM_QSTR(MP_QSTR_fontSize), MP_ROM_PTR(&display_tft_getFontSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&display_tft_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_orient), MP_ROM_PTR(&display_tft_setRot_obj) },
    { MP_ROM_QSTR(MP_QSTR_textWidth), MP_ROM_PTR(&display_tft_stringWidth_obj) },
    { MP_ROM_QSTR(MP_QSTR_textClear), MP_ROM_PTR(&display_tft_clearStringRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_attrib7seg), MP_ROM_PTR(&display_tft_7segAttrib_obj) },
    { MP_ROM_QSTR(MP_QSTR_image), MP_ROM_PTR(&display_tft_Image_obj) },
    { MP_ROM_QSTR(MP_QSTR_gettouch), MP_ROM_PTR(&display_tft_getTouch_obj) },
    { MP_ROM_QSTR(MP_QSTR_compileFont), MP_ROM_PTR(&display_tft_compileFont_obj) },
    { MP_ROM_QSTR(MP_QSTR_hsb2rgb), MP_ROM_PTR(&display_tft_HSBtoRGB_obj) },
    { MP_ROM_QSTR(MP_QSTR_setwin), MP_ROM_PTR(&display_tft_setclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_resetwin), MP_ROM_PTR(&display_tft_resetclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_savewin), MP_ROM_PTR(&display_tft_saveclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_restorewin), MP_ROM_PTR(&display_tft_restoreclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_screensize), MP_ROM_PTR(&display_tft_getSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_winsize), MP_ROM_PTR(&display_tft_getWinSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCalib), MP_ROM_PTR(&display_tft_setCalib_obj) },

    // class constants
    { MP_ROM_QSTR(MP_QSTR_ST7789), MP_ROM_INT(DISP_TYPE_ST7789V) },
    { MP_ROM_QSTR(MP_QSTR_ILI9341), MP_ROM_INT(DISP_TYPE_ILI9341) },
    { MP_ROM_QSTR(MP_QSTR_ILI9488), MP_ROM_INT(DISP_TYPE_ILI9488) },
    { MP_ROM_QSTR(MP_QSTR_ST7735), MP_ROM_INT(DISP_TYPE_ST7735) },
    { MP_ROM_QSTR(MP_QSTR_ST7735R), MP_ROM_INT(DISP_TYPE_ST7735R) },
    { MP_ROM_QSTR(MP_QSTR_ST7735B), MP_ROM_INT(DISP_TYPE_ST7735B) },

    { MP_ROM_QSTR(MP_QSTR_CENTER), MP_ROM_INT(CENTER) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT), MP_ROM_INT(RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_BOTTOM), MP_ROM_INT(BOTTOM) },
    { MP_ROM_QSTR(MP_QSTR_LASTX), MP_ROM_INT(LASTX) },
    { MP_ROM_QSTR(MP_QSTR_LASTY), MP_ROM_INT(LASTY) },

    { MP_ROM_QSTR(MP_QSTR_PORTRAIT), MP_ROM_INT(PORTRAIT) },
    { MP_ROM_QSTR(MP_QSTR_LANDSCAPE), MP_ROM_INT(LANDSCAPE) },
    { MP_ROM_QSTR(MP_QSTR_PORTRAIT_FLIP), MP_ROM_INT(PORTRAIT_FLIP) },
    { MP_ROM_QSTR(MP_QSTR_LANDSCAPE_FLIP), MP_ROM_INT(LANDSCAPE_FLIP) },

    { MP_ROM_QSTR(MP_QSTR_FONT_Default), MP_ROM_INT(DEFAULT_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_DejaVu18), MP_ROM_INT(DEJAVU18_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Dejavu24), MP_ROM_INT(DEJAVU24_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Ubuntu), MP_ROM_INT(UBUNTU16_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Comic), MP_ROM_INT(COMIC24_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Minya), MP_ROM_INT(MINYA24_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Tooney), MP_ROM_INT(TOONEY32_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Small), MP_ROM_INT(SMALL_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_DefaultSmall), MP_ROM_INT(DEF_SMALL_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_7seg), MP_ROM_INT(FONT_7SEG) },

	{ MP_ROM_QSTR(MP_QSTR_BLACK), MP_ROM_INT(iTFT_BLACK) },
	{ MP_ROM_QSTR(MP_QSTR_NAVY), MP_ROM_INT(iTFT_NAVY) },
	{ MP_ROM_QSTR(MP_QSTR_DARKGREEN), MP_ROM_INT(iTFT_DARKGREEN) },
	{ MP_ROM_QSTR(MP_QSTR_DARKCYAN), MP_ROM_INT(iTFT_DARKCYAN) },
	{ MP_ROM_QSTR(MP_QSTR_MAROON), MP_ROM_INT(iTFT_MAROON) },
	{ MP_ROM_QSTR(MP_QSTR_PURPLE), MP_ROM_INT(iTFT_PURPLE) },
	{ MP_ROM_QSTR(MP_QSTR_OLIVE), MP_ROM_INT(iTFT_OLIVE) },
	{ MP_ROM_QSTR(MP_QSTR_LIGHTGREY), MP_ROM_INT(iTFT_LIGHTGREY) },
	{ MP_ROM_QSTR(MP_QSTR_DARKGREY), MP_ROM_INT(iTFT_DARKGREY) },
	{ MP_ROM_QSTR(MP_QSTR_BLUE), MP_ROM_INT(iTFT_BLUE) },
	{ MP_ROM_QSTR(MP_QSTR_GREEN), MP_ROM_INT(iTFT_GREEN) },
	{ MP_ROM_QSTR(MP_QSTR_CYAN), MP_ROM_INT(iTFT_CYAN) },
	{ MP_ROM_QSTR(MP_QSTR_RED), MP_ROM_INT(iTFT_RED) },
	{ MP_ROM_QSTR(MP_QSTR_MAGENTA), MP_ROM_INT(iTFT_MAGENTA) },
	{ MP_ROM_QSTR(MP_QSTR_YELLOW), MP_ROM_INT(iTFT_YELLOW) },
	{ MP_ROM_QSTR(MP_QSTR_WHITE), MP_ROM_INT(iTFT_WHITE) },
	{ MP_ROM_QSTR(MP_QSTR_ORANGE), MP_ROM_INT(iTFT_ORANGE) },
	{ MP_ROM_QSTR(MP_QSTR_GREENYELLOW), MP_ROM_INT(iTFT_GREENYELLOW) },
	{ MP_ROM_QSTR(MP_QSTR_PINK), MP_ROM_INT(iTFT_PINK) },

	{ MP_ROM_QSTR(MP_QSTR_JPG), MP_ROM_INT(IMAGE_TYPE_JPG) },
	{ MP_ROM_QSTR(MP_QSTR_BMP), MP_ROM_INT(IMAGE_TYPE_BMP) },

	{ MP_ROM_QSTR(MP_QSTR_HSPI), MP_ROM_INT(LOBO_HSPI_HOST) },
	{ MP_ROM_QSTR(MP_QSTR_VSPI), MP_ROM_INT(LOBO_VSPI_HOST) },

	{ MP_ROM_QSTR(MP_QSTR_TOUCH_NONE), MP_ROM_INT(TOUCH_TYPE_NONE) },
	{ MP_ROM_QSTR(MP_QSTR_TOUCH_XPT), MP_ROM_INT(TOUCH_TYPE_XPT2046) },
	{ MP_ROM_QSTR(MP_QSTR_TOUCH_STMPE), MP_ROM_INT(TOUCH_TYPE_STMPE610) },
};
STATIC MP_DEFINE_CONST_DICT(display_tft_locals_dict, display_tft_locals_dict_table);

//======================================
const mp_obj_type_t display_tft_type = {
    { &mp_type_type },
    .name = MP_QSTR_TFT,
    .print = display_tft_printinfo,
    .make_new = display_tft_make_new,
    .locals_dict = (mp_obj_t)&display_tft_locals_dict,
};



//===============================================================
STATIC const mp_rom_map_elem_t display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_TFT), MP_ROM_PTR(&display_tft_type) },
};

//===============================================================================
STATIC MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

const mp_obj_module_t mp_module_display = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&display_module_globals,
};

#endif // CONFIG_MICROPY_USE_DISPLAY
