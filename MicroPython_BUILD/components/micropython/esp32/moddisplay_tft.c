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

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_TFT

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#include "driver/gpio.h"

#include "tft/tftspi.h"
#include "tft/tft.h"
#include "extmod/vfs_native.h"
#include "machine_hw_spi.h"
#include "modmachine.h"

extern uint8_t disp_used_spi_host;

typedef struct _display_tft_obj_t {
    mp_obj_base_t base;
    machine_hw_spi_obj_t *spi;
    display_config_t dconfig;
    exspi_device_handle_t disp_spi_dev;
    exspi_device_handle_t ts_spi_dev;
    exspi_device_handle_t *disp_spi;
    exspi_device_handle_t *ts_spi;
    uint32_t tp_calx;
    uint32_t tp_caly;
} display_tft_obj_t;

const mp_obj_type_t display_tft_type;

static const char* const display_types[] = {
    "ILI9341",
    "ILI9488",
    "ST7789V",
    "ST7735",
    "ST7735R",
    "ST7735B",
    "M5STACK",
    "Unknown",
};

static const char* const touch_types[] = {
    "None",
    "xpt2046",
    "stmpe610",
    "Unknown",
};

// constructor(id, ...)
//-----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {

    display_tft_obj_t *self = m_new_obj(display_tft_obj_t);
    self->base.type = &display_tft_type;
    self->spi = NULL;
    self->disp_spi_dev.handle = NULL;
    self->disp_spi_dev.cs = -1;
    self->disp_spi_dev.dc = -1;
    self->disp_spi_dev.selected = 0;
    self->ts_spi_dev.handle = NULL;
    self->ts_spi_dev.cs = -1;
    self->ts_spi_dev.dc = -1;
    self->ts_spi_dev.selected = 0;
    self->disp_spi = &self->disp_spi_dev;
    self->ts_spi = &self->ts_spi_dev;

    return MP_OBJ_FROM_PTR(self);
}

//-----------------------------------------------------------------------------------------------
STATIC void display_tft_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    display_tft_obj_t *self = self_in;
    if (self->disp_spi->handle) {
        mp_printf(print, "TFT   (%dx%d, Type=%s, Ready: %s, Color mode: %d-bit, Clk=%u Hz, RdClk=%u Hz, Touch: %s)\n",
                self->dconfig.width, self->dconfig.height, display_types[self->dconfig.type], ((self->disp_spi->handle) ? "yes" : "no"), self->dconfig.color_bits, self->dconfig.speed, self->dconfig.rdspeed, ((self->ts_spi->handle) ? "yes" : "no"));
        mp_printf(print, "Pins  (miso=%d, mosi=%d, clk=%d, cs=%d, dc=%d, reset=%d, backlight=%d)", self->dconfig.miso, self->dconfig.mosi, self->dconfig.sck, self->dconfig.cs, self->dconfig.dc, self->dconfig.rst, self->dconfig.bckl);
        if (self->ts_spi->handle) {
            mp_printf(print, "\nTouch (Enabled, type: %s, cs=%d)", touch_types[self->dconfig.touch], self->dconfig.tcs);
        }
    }
    else {
        mp_printf(print, "TFT (Not initialized)");
    }
}

/*
 * tftspi.c low level driver uses some global variables
 * Here we set those variables so that multiple displays can be used
 */
//-------------------------------------------------
static int setupDevice(display_tft_obj_t *disp_dev)
{
    if (tft_active_mode == TFT_MODE_EVE) return 0;

    if (disp_dev->disp_spi->handle == NULL) return 1;

    if (disp_spi != disp_dev->disp_spi) {
        disp_spi = disp_dev->disp_spi;
        ts_spi = disp_dev->ts_spi;
        TFT_display_setvars(&disp_dev->dconfig);

        tp_calx = disp_dev->tp_calx;
        tp_caly = disp_dev->tp_caly;
        spi_device_select(disp_spi, 1);
        spi_device_deselect(disp_spi);
    }

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

//------------------------------------------------------
STATIC void spi_deinit_internal(display_tft_obj_t *self)
{
    if (self->disp_spi->handle) {
        esp_err_t ret;
        // Deinitialize display spi device(s)
        if (self->ts_spi->handle) {
            ret = remove_extspi_device(self->ts_spi);
            if (ret != ESP_OK) {
                mp_raise_msg(&mp_type_OSError, "Error removing touch device");
            }
        }
        ret = remove_extspi_device(self->disp_spi);
        if (ret != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "Error removing display device");
        }

        gpio_pad_select_gpio(self->dconfig.miso);
        gpio_pad_select_gpio(self->dconfig.mosi);
        gpio_pad_select_gpio(self->dconfig.sck);
    }
}


//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_init(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_type, ARG_host, ARG_width, ARG_height, ARG_speed, ARG_miso, ARG_mosi, ARG_clk, ARG_cs,
        ARG_dc, ARG_tcs, ARG_rst, ARG_bckl, ARG_bcklon, ARG_hastouch, ARG_invrot, ARG_bgr, ARG_cbits, ARG_rot, ARG_splash };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_type,      MP_ARG_REQUIRED                   | MP_ARG_INT,  { .u_int = DISP_TYPE_ST7789V } },
        { MP_QSTR_spihost,                     MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = HSPI_HOST } },
        { MP_QSTR_width,                       MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = DEFAULT_TFT_DISPLAY_WIDTH } },
        { MP_QSTR_height,                      MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = DEFAULT_TFT_DISPLAY_HEIGHT } },
        { MP_QSTR_speed,                       MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 10000000 } },
        { MP_QSTR_miso,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_mosi,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_clk,       MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_cs,        MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_dc,        MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_tcs,                         MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_rst_pin,                     MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_backl_pin,                   MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_backl_on,                    MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_hastouch,                    MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = TOUCH_TYPE_NONE } },
        { MP_QSTR_invrot,                      MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_bgr,                         MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = false } },
        { MP_QSTR_color_bits,                  MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 24 } },
        { MP_QSTR_rot,                         MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_splash,                      MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = true } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_tft_obj_t *self = pos_args[0];
    esp_err_t ret;

    // === deinitialize display spi device if it was initialized ===
    if (self->disp_spi->handle) spi_deinit_internal(self);

    // === Get arguments ===
    if ((args[ARG_host].u_int != HSPI_HOST) && (args[ARG_host].u_int != VSPI_HOST)) {
        mp_raise_ValueError("SPI host must be either HSPI(1) or VSPI(2)");
    }
    if ((SPIbus_configs[VSPI_HOST] == NULL) && (args[ARG_host].u_int == VSPI_HOST)) {
        mp_raise_ValueError("SPI host must be HSPI(1), VSPI(2) used by SPIRAM");
    }

    if ((args[ARG_type].u_int < 0) || (args[ARG_type].u_int >= DISP_TYPE_MAX)) {
        mp_raise_ValueError("Unsupported display type");
    }

    if ((args[ARG_cbits].u_int != 16) && (args[ARG_cbits].u_int != 24)) {
        mp_raise_ValueError("Unsupported color bits");
    }

    self->dconfig.color_bits = args[ARG_cbits].u_int;

    self->dconfig.type = args[ARG_type].u_int;

    if ((args[ARG_hastouch].u_int == TOUCH_TYPE_XPT2046) || (args[ARG_hastouch].u_int == TOUCH_TYPE_STMPE610)) {
        if (args[ARG_tcs].u_int < 0) {
            mp_raise_ValueError("Touch selected but no touch cs given");
        }
        self->dconfig.touch = args[ARG_hastouch].u_int;
        if (args[ARG_hastouch].u_int == TOUCH_TYPE_XPT2046) {
            self->tp_calx = TP_CALX_XPT2046;
            self->tp_caly = TP_CALY_XPT2046;
        }
        else {
            self->tp_calx = TP_CALX_STMPE610;
            self->tp_caly = TP_CALY_STMPE610;
        }
        self->dconfig.tcs = args[ARG_tcs].u_int;
    }
    else self->dconfig.touch = TOUCH_TYPE_NONE;

    self->dconfig.host = args[ARG_host].u_int;
    self->dconfig.gamma = 0;
    self->dconfig.width = args[ARG_width].u_int;   // smaller dimension
    self->dconfig.height = args[ARG_height].u_int; // larger dimension
    self->dconfig.rdspeed = 8000000;
    if (args[ARG_invrot].u_int >= 0) self->dconfig.invrot = args[ARG_invrot].u_int;
    else {
        if ((self->dconfig.type == DISP_TYPE_ST7789V) ||
                (self->dconfig.type == DISP_TYPE_ST7735) ||
                (self->dconfig.type == DISP_TYPE_ST7735R) ||
                (self->dconfig.type == DISP_TYPE_ST7735B)) self->dconfig.invrot = 1;
        else if (self->dconfig.type == DISP_TYPE_M5STACK) self->dconfig.invrot = 3;
        else self->dconfig.invrot = 0;
    }

    if (args[ARG_bgr].u_bool) self->dconfig.bgr = 8;
    else self->dconfig.bgr = 0;

    self->dconfig.rst = args[ARG_rst].u_int;
    self->dconfig.bckl = args[ARG_bckl].u_int;
    self->dconfig.bckl_on = args[ARG_bcklon].u_int & 1;

    self->dconfig.miso = args[ARG_miso].u_int;
    self->dconfig.mosi = args[ARG_mosi].u_int;
    self->dconfig.sck = args[ARG_clk].u_int;

    self->dconfig.cs = args[ARG_cs].u_int;
    self->dconfig.dc = args[ARG_dc].u_int;

    disp_spi = self->disp_spi;
    ts_spi = self->ts_spi;

    int orient = args[ARG_rot].u_int;
    if (orient < 0) {
        if (self->dconfig.type == DISP_TYPE_M5STACK) orient = LANDSCAPE;
        else orient = PORTRAIT;
    }
    else orient &= 3;

    // ================================
    // ==== Initialize the Display ====
    ret = TFT_display_init(&self->dconfig);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Error initializing display");
    }

    disp_used_spi_host = args[ARG_host].u_int;

    if (self->dconfig.type == DISP_TYPE_GENERIC) return mp_const_none;

    // ==== Set SPI clock used for display operations ====
    self->dconfig.speed = spi_set_speed(self->disp_spi, args[ARG_speed].u_int);

    max_rdclock = find_rd_speed();
    self->dconfig.rdspeed = max_rdclock;

    font_rotate = 0;
    text_wrap = 0;
    font_transparent = 0;
    font_forceFixed = 0;
    gray_scale = 0;
    TFT_setRotation(orient);
    self->dconfig.width = _width;
    self->dconfig.height = _height;
    TFT_setGammaCurve(0);
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

    bcklOn(&self->dconfig);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_init_obj, 0, display_tft_init);

//--------------------------------------------------
STATIC mp_obj_t display_tft_deinit(mp_obj_t self_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self) == 0) {
        spi_deinit_internal(self);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_deinit_obj, display_tft_deinit);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawPixel_obj, 2, display_tft_drawPixel);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawLine_obj, 4, display_tft_drawLine);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawLineByAngle_obj, 5, display_tft_drawLineByAngle);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawTriangle_obj, 6, display_tft_drawTriangle);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawCircle_obj, 3, display_tft_drawCircle);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawEllipse_obj, 4, display_tft_drawEllipse);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawArc_obj, 6, display_tft_drawArc);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawPoly_obj, 5, display_tft_drawPoly);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawRect_obj, 4, display_tft_drawRect);

//--------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_readScreen(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,      MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,  MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height, MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_buffer,                   MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    mp_int_t w = args[2].u_int;
    mp_int_t h = args[3].u_int;

    // clipping
    if ((x >= _width) || (y > _height)) {
        mp_raise_ValueError("Point (x,y) outside the display area");
    }

    if (x < 0) {
        w -= (0 - x);
        x = 0;
    }
    if (y < 0) {
        h -= (0 - y);
        y = 0;
    }
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    if ((x + w) > (_width+1)) w = _width - x + 1;
    if ((y + h) > (_height+1)) h = _height - y + 1;
    if (w == 0) w = 1;
    if (h == 0) h = 1;

    int clr_len = h*w;
    int buf_len = (clr_len*3) + 1;
    uint8_t *buf = NULL;
    vstr_t vstr;

    if (args[4].u_obj == mp_const_none) {
        vstr_init_len(&vstr, buf_len);
        buf = (uint8_t *)vstr.buf;
    }
    else {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[4].u_obj, &bufinfo, MP_BUFFER_WRITE);
        if (bufinfo.len != buf_len) {
            mp_raise_ValueError("Wrong buffer length");
        }
        buf = (uint8_t *)bufinfo.buf;
    }
    memset(buf, 0, buf_len);

    esp_err_t ret = read_data(x, y, x+w+1, y+h+1, (uint32_t)clr_len, buf, 1);

    if (ret == ESP_OK) {
        if (args[4].u_obj == mp_const_none) return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
        else return mp_const_true;
    }
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_readScreen_obj, 4, display_tft_readScreen);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_drawRoundRect_obj, 5, display_tft_drawRoundRect);

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
    if (args[2].u_int >= 0) font_transparent = args[2].u_int & 1;
    if (args[3].u_int >= 0) font_forceFixed = args[3].u_int & 1;

    if (font == FONT_7SEG) {
        set_7seg_font_atrib(args[4].u_int, args[5].u_int, (int)args[6].u_bool, intToColor(args[7].u_int));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_setFont_obj, 1, display_tft_setFont);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_getFontSize_obj, 0, display_tft_getFontSize);

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
    self->dconfig.width = _width;
    self->dconfig.height = _height;

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
        { MP_QSTR_transparent,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fixedwidth,   MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_wrap,         MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_bgcolor,      MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    color_t old_fg = _fg;
    color_t old_bg = _bg;
    int old_rot = font_rotate;
    int old_transp = font_transparent;
    int old_fixed = font_forceFixed;
    int old_wrap = text_wrap;

    mp_int_t x = args[0].u_int;
    mp_int_t y = args[1].u_int;
    char *st = (char *)mp_obj_str_get_str(args[2].u_obj);

    if (args[3].u_int >= 0) _fg = intToColor(args[3].u_int);
    if (args[4].u_int >= 0) font_rotate = args[4].u_int;
    if (args[5].u_int >= 0) font_transparent = args[5].u_int & 1;
    if (args[6].u_int >= 0) font_forceFixed = args[6].u_int & 1;
    if (args[7].u_int >= 0) text_wrap = args[7].u_int & 1;
    if (args[8].u_int >= 0) _bg = intToColor(args[8].u_int);

    TFT_print(st, x, y);

    _fg = old_fg;
    _bg = old_bg;
    font_rotate = old_rot;
    font_transparent = old_transp;
    font_forceFixed = old_fixed;
    text_wrap = old_wrap;

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_print_obj, 3, display_tft_print);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_stringWidth_obj, 1, display_tft_stringWidth);

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

    if (args[3].u_int >= 0) _bg = intToColor(args[3].u_int);

    TFT_clearStringRect(x, y, st);

    _bg = old_bg;

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_clearStringRect_obj, 3, display_tft_clearStringRect);

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
        { MP_QSTR_raw,                   MP_ARG_BOOL, { .u_bool = false } },
        { MP_QSTR_wait, MP_ARG_KW_ONLY | MP_ARG_INT, { .u_int = 0 } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    if (setupDevice(self)) return mp_const_none;
    if (self->ts_spi->handle == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Touch not configured"));
    }

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int x = 0;
    int y = 0;
    uint8_t raw = 0;
    if (args[0].u_bool) raw = 1;
    int wait = args[1].u_int;
    if ((wait < 5) || (wait > 60000)) wait = 0;

    int res = TFT_read_touch(&x, &y, raw);
    if (wait) {
        #ifdef CONFIG_MICROPY_USE_TASK_WDT
        esp_task_wdt_reset();
        #endif
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t tstart = ((uint32_t)tv.tv_sec * 1000) + ((uint32_t)tv.tv_usec / 1000);
        uint32_t tend = tstart;
        uint32_t nres = tstart;

        // wait until not touched
        while ((tend-tstart) < wait) {
            res = TFT_read_touch(&x, &y, raw);
            if (res == 0) break;
            vTaskDelay(2);
            gettimeofday(&tv, NULL);
            tend = ((uint32_t)tv.tv_sec * 1000) + ((uint32_t)tv.tv_usec / 1000);
            #ifdef CONFIG_MICROPY_USE_TASK_WDT
            if ((tend-nres) > (CONFIG_TASK_WDT_TIMEOUT_S*500)) {
                esp_task_wdt_reset();
                nres = tend;
            }
            #endif
        }
        // wait until touched
        while ((tend-tstart) < wait) {
            res = TFT_read_touch(&x, &y, raw);
            if (res) break;
            vTaskDelay(2);
            gettimeofday(&tv, NULL);
            tend = ((uint32_t)tv.tv_sec * 1000) + ((uint32_t)tv.tv_usec / 1000);
            #ifdef CONFIG_MICROPY_USE_TASK_WDT
            if ((tend-nres) > (CONFIG_TASK_WDT_TIMEOUT_S*500)) {
                esp_task_wdt_reset();
                nres = tend;
            }
            #endif
        }
    }

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
    //display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_setclipwin_obj, 4, display_tft_setclipwin);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_resetclipwin(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;

    TFT_resetclipwin();

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_resetclipwin_obj, 0, display_tft_resetclipwin);

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
MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_getSize_obj, 0, display_tft_getSize);

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
        { MP_QSTR_calx,                      MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_caly,                      MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_from_nvs, MP_ARG_KW_ONLY | MP_ARG_BOOL, { .u_bool = false } },
    };
    display_tft_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (setupDevice(self)) return mp_const_none;
    if (self->ts_spi->handle == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Touch not configured"));
    }

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (self->dconfig.touch == TOUCH_TYPE_NONE) {
        return mp_const_none;
    }

    if (args[2].u_bool) {
        if (mpy_nvs_handle == 0) {
            mp_raise_msg(&mp_type_OSError, "NVS not available!");
        }
        int calx = 0, caly = 0;
        bool f = true;
        if (ESP_ERR_NVS_NOT_FOUND == nvs_get_i32(mpy_nvs_handle, "tpcalibX", &calx)) f = false;
        if (f) {
            if (ESP_ERR_NVS_NOT_FOUND == nvs_get_i32(mpy_nvs_handle, "tpcalibY", &caly)) f = false;
        }
        if (!f) {
            mp_raise_msg(&mp_type_OSError, "Calibration values not found in NVS");
        }
        self->tp_calx = calx;
        self->tp_caly = caly;
        return mp_const_none;
    }

    if (args[0].u_int == 0) {
        if (self->dconfig.touch == TOUCH_TYPE_XPT2046) self->tp_calx = TP_CALX_XPT2046;
        else self->tp_calx = TP_CALX_STMPE610;
    }
    else self->tp_calx = args[0].u_int;

    if (args[1].u_int == 0) {
        if (self->dconfig.touch == TOUCH_TYPE_XPT2046) self->tp_caly = TP_CALY_XPT2046;
        else self->tp_caly = TP_CALY_STMPE610;
    }
    else self->tp_caly = args[1].u_int;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_tft_setCalib_obj, 0, display_tft_setCalib);

//----------------------------------------------------
STATIC mp_obj_t display_tft_getCalib(mp_obj_t self_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;
    if (self->ts_spi->handle == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Touch not configured"));
    }
    mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int(self->tp_calx);
    tuple[1] = mp_obj_new_int(self->tp_caly);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_getCalib_obj, display_tft_getCalib);


//------------------------------------------------------------------------
STATIC mp_obj_t display_tft_backlight(mp_obj_t self_in, mp_obj_t onoff_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    int onoff = mp_obj_get_int(onoff_in);
    if (onoff) bcklOn(&self->dconfig);
    else bcklOff(&self->dconfig);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(display_tft_backlight_obj, display_tft_backlight);

//------------------------------------------------------
STATIC mp_obj_t display_tft_touch_type(mp_obj_t self_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    return mp_obj_new_int(self->dconfig.touch);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_touch_type_obj, display_tft_touch_type);


// ==== Low level functions ======================================

//------------------------------------------------------------------------
STATIC mp_obj_t display_tft_set_speed(mp_obj_t self_in, mp_obj_t speed_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    int speed = mp_obj_get_int(speed_in);

    // Set SPI clock used for display operations
    self->dconfig.speed = spi_set_speed(self->disp_spi, speed);

    max_rdclock = find_rd_speed();
    self->dconfig.rdspeed = max_rdclock;

    mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int(self->dconfig.speed);
    tuple[1] = mp_obj_new_int(self->dconfig.rdspeed);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(display_tft_set_speed_obj, display_tft_set_speed);

//--------------------------------------------------
STATIC mp_obj_t display_tft_select(mp_obj_t self_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    disp_select();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_select_obj, display_tft_select);

//----------------------------------------------------
STATIC mp_obj_t display_tft_deselect(mp_obj_t self_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    disp_deselect();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_deselect_obj, display_tft_deselect);

//--------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_cmd_read(mp_obj_t self_in, mp_obj_t cmd_in, mp_obj_t len_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    uint8_t cmd = (uint8_t)mp_obj_get_int(cmd_in);
    uint8_t len = (uint8_t)mp_obj_get_int(len_in);
    if ((len < 1) || (len > 4)) len = 1;

    uint32_t res = read_cmd(cmd, len);

    return mp_obj_new_int_from_uint(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(display_tft_cmd_read_obj, display_tft_cmd_read);

//-------------------------------------------------------------------------
STATIC mp_obj_t display_tft_send_command(mp_obj_t self_in, mp_obj_t cmd_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    uint8_t cmd = (uint8_t)mp_obj_get_int(cmd_in);

    disp_select();
    disp_spi_transfer_cmd(cmd);
    wait_trans_finish(0);
    disp_deselect();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(display_tft_send_command_obj, display_tft_send_command);

//--------------------------------------------------------------------------------------------
STATIC mp_obj_t display_tft_send_cmd_data(mp_obj_t self_in, mp_obj_t cmd_in, mp_obj_t data_in)
{
    display_tft_obj_t *self = self_in;
    if (setupDevice(self)) return mp_const_none;

    uint8_t cmd = (uint8_t)mp_obj_get_int(cmd_in);
    mp_buffer_info_t data;
    mp_get_buffer_raise(data_in, &data, MP_BUFFER_READ);

    disp_select();
    disp_spi_transfer_cmd_data(cmd, data.buf, data.len);
    wait_trans_finish(0);
    disp_deselect();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(display_tft_send_cmd_data_obj, display_tft_send_cmd_data);

//--------------------------------------------------
STATIC mp_obj_t display_tft_get_bg(mp_obj_t self_in)
{
    int icolor = (int)((_bg.r << 16) | (_bg.g << 8) | _bg.b);
    return mp_obj_new_int(icolor);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_get_bg_obj, display_tft_get_bg);

//--------------------------------------------------
STATIC mp_obj_t display_tft_get_fg(mp_obj_t self_in)
{
    int icolor = (int)((_fg.r << 16) | (_fg.g << 8) | _fg.b);
    return mp_obj_new_int(icolor);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_get_fg_obj, display_tft_get_fg);

//---------------------------------------------------------------------
STATIC mp_obj_t display_tft_set_bg(mp_obj_t self_in, mp_obj_t color_in)
{
    color_t color = intToColor(mp_obj_get_int(color_in));
    _bg = color;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(display_tft_set_bg_obj, display_tft_set_bg);

//---------------------------------------------------------------------
STATIC mp_obj_t display_tft_set_fg(mp_obj_t self_in, mp_obj_t color_in)
{
    color_t color = intToColor(mp_obj_get_int(color_in));
    _fg = color;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(display_tft_set_fg_obj, display_tft_set_fg);

//-------------------------------------------------
STATIC mp_obj_t display_tft_get_X(mp_obj_t self_in)
{
    int x = TFT_X;
    return mp_obj_new_int(x);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_get_X_obj, display_tft_get_X);

//-------------------------------------------------
STATIC mp_obj_t display_tft_get_Y(mp_obj_t self_in)
{
    int y = TFT_Y;
    return mp_obj_new_int(y);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_tft_get_Y_obj, display_tft_get_Y);


//================================================================
STATIC const mp_rom_map_elem_t display_tft_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init),                MP_ROM_PTR(&display_tft_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),              MP_ROM_PTR(&display_tft_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel),               MP_ROM_PTR(&display_tft_drawPixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_readPixel),           MP_ROM_PTR(&display_tft_readPixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_line),                MP_ROM_PTR(&display_tft_drawLine_obj) },
    { MP_ROM_QSTR(MP_QSTR_lineByAngle),         MP_ROM_PTR(&display_tft_drawLineByAngle_obj) },
    { MP_ROM_QSTR(MP_QSTR_triangle),            MP_ROM_PTR(&display_tft_drawTriangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle),              MP_ROM_PTR(&display_tft_drawCircle_obj) },
    { MP_ROM_QSTR(MP_QSTR_ellipse),             MP_ROM_PTR(&display_tft_drawEllipse_obj) },
    { MP_ROM_QSTR(MP_QSTR_arc),                 MP_ROM_PTR(&display_tft_drawArc_obj) },
    { MP_ROM_QSTR(MP_QSTR_polygon),             MP_ROM_PTR(&display_tft_drawPoly_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect),                MP_ROM_PTR(&display_tft_drawRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_readScreen),          MP_ROM_PTR(&display_tft_readScreen_obj) },
    { MP_ROM_QSTR(MP_QSTR_roundrect),           MP_ROM_PTR(&display_tft_drawRoundRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),               MP_ROM_PTR(&display_tft_fillScreen_obj) },
    { MP_ROM_QSTR(MP_QSTR_clearwin),            MP_ROM_PTR(&display_tft_fillWin_obj) },
    { MP_ROM_QSTR(MP_QSTR_font),                MP_ROM_PTR(&display_tft_setFont_obj) },
    { MP_ROM_QSTR(MP_QSTR_fontSize),            MP_ROM_PTR(&display_tft_getFontSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_text),                MP_ROM_PTR(&display_tft_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_orient),              MP_ROM_PTR(&display_tft_setRot_obj) },
    { MP_ROM_QSTR(MP_QSTR_textWidth),           MP_ROM_PTR(&display_tft_stringWidth_obj) },
    { MP_ROM_QSTR(MP_QSTR_textClear),           MP_ROM_PTR(&display_tft_clearStringRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_attrib7seg),          MP_ROM_PTR(&display_tft_7segAttrib_obj) },
    { MP_ROM_QSTR(MP_QSTR_image),               MP_ROM_PTR(&display_tft_Image_obj) },
    { MP_ROM_QSTR(MP_QSTR_gettouch),            MP_ROM_PTR(&display_tft_getTouch_obj) },
    { MP_ROM_QSTR(MP_QSTR_compileFont),         MP_ROM_PTR(&display_tft_compileFont_obj) },
    { MP_ROM_QSTR(MP_QSTR_hsb2rgb),             MP_ROM_PTR(&display_tft_HSBtoRGB_obj) },
    { MP_ROM_QSTR(MP_QSTR_setwin),              MP_ROM_PTR(&display_tft_setclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_resetwin),            MP_ROM_PTR(&display_tft_resetclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_savewin),             MP_ROM_PTR(&display_tft_saveclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_restorewin),          MP_ROM_PTR(&display_tft_restoreclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_screensize),          MP_ROM_PTR(&display_tft_getSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_winsize),             MP_ROM_PTR(&display_tft_getWinSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCalib),            MP_ROM_PTR(&display_tft_setCalib_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCalib),            MP_ROM_PTR(&display_tft_getCalib_obj) },
    { MP_ROM_QSTR(MP_QSTR_backlight),           MP_ROM_PTR(&display_tft_backlight_obj) },
    { MP_ROM_QSTR(MP_QSTR_getTouchType),        MP_ROM_PTR(&display_tft_touch_type_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_fg),              MP_ROM_PTR(&display_tft_get_fg_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_bg),              MP_ROM_PTR(&display_tft_get_bg_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_fg),              MP_ROM_PTR(&display_tft_set_fg_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_bg),              MP_ROM_PTR(&display_tft_set_bg_obj) },
    { MP_ROM_QSTR(MP_QSTR_text_x),              MP_ROM_PTR(&display_tft_get_X_obj) },
    { MP_ROM_QSTR(MP_QSTR_text_y),              MP_ROM_PTR(&display_tft_get_Y_obj) },

    { MP_ROM_QSTR(MP_QSTR_tft_setspeed),        MP_ROM_PTR(&display_tft_set_speed_obj) },
    { MP_ROM_QSTR(MP_QSTR_tft_select),          MP_ROM_PTR(&display_tft_select_obj) },
    { MP_ROM_QSTR(MP_QSTR_tft_deselect),        MP_ROM_PTR(&display_tft_deselect_obj) },
    { MP_ROM_QSTR(MP_QSTR_tft_writecmd),        MP_ROM_PTR(&display_tft_send_command_obj) },
    { MP_ROM_QSTR(MP_QSTR_tft_writecmddata),    MP_ROM_PTR(&display_tft_send_cmd_data_obj) },
    { MP_ROM_QSTR(MP_QSTR_tft_readcmd),         MP_ROM_PTR(&display_tft_cmd_read_obj) },

    // class constants
    { MP_ROM_QSTR(MP_QSTR_ST7789),              MP_ROM_INT(DISP_TYPE_ST7789V) },
    { MP_ROM_QSTR(MP_QSTR_ILI9341),             MP_ROM_INT(DISP_TYPE_ILI9341) },
    { MP_ROM_QSTR(MP_QSTR_ILI9488),             MP_ROM_INT(DISP_TYPE_ILI9488) },
    { MP_ROM_QSTR(MP_QSTR_ST7735),              MP_ROM_INT(DISP_TYPE_ST7735) },
    { MP_ROM_QSTR(MP_QSTR_ST7735R),             MP_ROM_INT(DISP_TYPE_ST7735R) },
    { MP_ROM_QSTR(MP_QSTR_ST7735B),             MP_ROM_INT(DISP_TYPE_ST7735B) },
    { MP_ROM_QSTR(MP_QSTR_M5STACK),             MP_ROM_INT(DISP_TYPE_M5STACK) },
    { MP_ROM_QSTR(MP_QSTR_GENERIC),             MP_ROM_INT(DISP_TYPE_GENERIC) },

    { MP_ROM_QSTR(MP_QSTR_CENTER),              MP_ROM_INT(CENTER) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT),               MP_ROM_INT(RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_BOTTOM),              MP_ROM_INT(BOTTOM) },
    { MP_ROM_QSTR(MP_QSTR_LASTX),               MP_ROM_INT(LASTX) },
    { MP_ROM_QSTR(MP_QSTR_LASTY),               MP_ROM_INT(LASTY) },

    { MP_ROM_QSTR(MP_QSTR_PORTRAIT),            MP_ROM_INT(PORTRAIT) },
    { MP_ROM_QSTR(MP_QSTR_LANDSCAPE),           MP_ROM_INT(LANDSCAPE) },
    { MP_ROM_QSTR(MP_QSTR_PORTRAIT_FLIP),       MP_ROM_INT(PORTRAIT_FLIP) },
    { MP_ROM_QSTR(MP_QSTR_LANDSCAPE_FLIP),      MP_ROM_INT(LANDSCAPE_FLIP) },

    { MP_ROM_QSTR(MP_QSTR_FONT_Default),        MP_ROM_INT(DEFAULT_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_DejaVu18),       MP_ROM_INT(DEJAVU18_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_DejaVu24),       MP_ROM_INT(DEJAVU24_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Ubuntu),         MP_ROM_INT(UBUNTU16_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Comic),          MP_ROM_INT(COMIC24_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Minya),          MP_ROM_INT(MINYA24_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Tooney),         MP_ROM_INT(TOONEY32_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_Small),          MP_ROM_INT(SMALL_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_DefaultSmall),   MP_ROM_INT(DEF_SMALL_FONT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_7seg),           MP_ROM_INT(FONT_7SEG) },

    { MP_ROM_QSTR(MP_QSTR_BLACK),               MP_ROM_INT(iTFT_BLACK) },
    { MP_ROM_QSTR(MP_QSTR_NAVY),                MP_ROM_INT(iTFT_NAVY) },
    { MP_ROM_QSTR(MP_QSTR_DARKGREEN),           MP_ROM_INT(iTFT_DARKGREEN) },
    { MP_ROM_QSTR(MP_QSTR_DARKCYAN),            MP_ROM_INT(iTFT_DARKCYAN) },
    { MP_ROM_QSTR(MP_QSTR_MAROON),              MP_ROM_INT(iTFT_MAROON) },
    { MP_ROM_QSTR(MP_QSTR_PURPLE),              MP_ROM_INT(iTFT_PURPLE) },
    { MP_ROM_QSTR(MP_QSTR_OLIVE),               MP_ROM_INT(iTFT_OLIVE) },
    { MP_ROM_QSTR(MP_QSTR_LIGHTGREY),           MP_ROM_INT(iTFT_LIGHTGREY) },
    { MP_ROM_QSTR(MP_QSTR_DARKGREY),            MP_ROM_INT(iTFT_DARKGREY) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),                MP_ROM_INT(iTFT_BLUE) },
    { MP_ROM_QSTR(MP_QSTR_GREEN),               MP_ROM_INT(iTFT_GREEN) },
    { MP_ROM_QSTR(MP_QSTR_CYAN),                MP_ROM_INT(iTFT_CYAN) },
    { MP_ROM_QSTR(MP_QSTR_RED),                 MP_ROM_INT(iTFT_RED) },
    { MP_ROM_QSTR(MP_QSTR_MAGENTA),             MP_ROM_INT(iTFT_MAGENTA) },
    { MP_ROM_QSTR(MP_QSTR_YELLOW),              MP_ROM_INT(iTFT_YELLOW) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),               MP_ROM_INT(iTFT_WHITE) },
    { MP_ROM_QSTR(MP_QSTR_ORANGE),              MP_ROM_INT(iTFT_ORANGE) },
    { MP_ROM_QSTR(MP_QSTR_GREENYELLOW),         MP_ROM_INT(iTFT_GREENYELLOW) },
    { MP_ROM_QSTR(MP_QSTR_PINK),                MP_ROM_INT(iTFT_PINK) },

    { MP_ROM_QSTR(MP_QSTR_COLOR_BITS16),        MP_ROM_INT(16) },
    { MP_ROM_QSTR(MP_QSTR_COLOR_BITS24),        MP_ROM_INT(24) },

    { MP_ROM_QSTR(MP_QSTR_JPG),                 MP_ROM_INT(IMAGE_TYPE_JPG) },
    { MP_ROM_QSTR(MP_QSTR_BMP),                 MP_ROM_INT(IMAGE_TYPE_BMP) },

    { MP_ROM_QSTR(MP_QSTR_HSPI),                MP_ROM_INT(HSPI_HOST) },
    { MP_ROM_QSTR(MP_QSTR_VSPI),                MP_ROM_INT(VSPI_HOST) },

    { MP_ROM_QSTR(MP_QSTR_TOUCH_NONE),          MP_ROM_INT(TOUCH_TYPE_NONE) },
    { MP_ROM_QSTR(MP_QSTR_TOUCH_XPT),           MP_ROM_INT(TOUCH_TYPE_XPT2046) },
    { MP_ROM_QSTR(MP_QSTR_TOUCH_STMPE),         MP_ROM_INT(TOUCH_TYPE_STMPE610) },
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

#endif // CONFIG_MICROPY_USE_TFT

