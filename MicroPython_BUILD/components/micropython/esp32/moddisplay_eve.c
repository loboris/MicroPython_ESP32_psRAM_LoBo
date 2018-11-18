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

#if CONFIG_MICROPY_USE_EVE

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "py/obj.h"
#include "py/objarray.h"

#include "driver/gpio.h"
#include "extmod/vfs_native.h"
#include "machine_hw_spi.h"
#include "modmachine.h"
#include "mphalport.h"
#include "tft/tft.h"
#include "moddisplay_tft.h"
#include "eve/FT8.h"


#define FT8_ENABLE_PNG_LOADING  1
#define IMAGE_TYPE_PNG  0
#define IMAGE_TYPE_JPG  1
#define IMAGE_TYPE_RAW  2
#define IMAGE_TYPE_BIN  3
#define IMAGE_TYPE_NONE 4

typedef struct _display_eve_obj_t {
    mp_obj_base_t base;
    machine_hw_spi_obj_t *spi;
    eve_config_t dconfig;
    exspi_device_handle_t disp_spi_dev;
    exspi_device_handle_t *disp_spi;
    uint16_t width;
    uint16_t height;
    uint8_t in_list;
} display_eve_obj_t;

typedef struct _eve_font_metrics_t {
    uint8_t widths[EVE_FONT_WIDTHS_SIZE];
    uint32_t format;
    uint32_t stride;
    uint32_t width;
    uint32_t height;
    int32_t ptr;
} eve_font_metrics_t;

typedef struct _font_eve_obj_t {
    mp_obj_base_t base;
    uint32_t addr;
    uint32_t size;
    eve_font_metrics_t metrics;
    uint8_t handle;
    uint8_t format;
    uint8_t maxwidth;
    uint8_t minwidth;
    uint8_t nchars;
    uint8_t firstc;
    uint8_t loaded;
} font_eve_obj_t;

typedef struct _image_eve_obj_t {
    mp_obj_base_t base;
    uint32_t addr;
    uint32_t size;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint8_t orig_fmt;
    uint8_t loaded;
} image_eve_obj_t;

typedef struct _console_eve_obj_t {
    mp_obj_base_t base;
    uint32_t    addr;
    uint32_t    size;
    uint32_t    curr_addr;
    uint16_t    x;
    uint16_t    y;
    uint16_t    width;
    uint16_t    height;
    uint16_t    rowbytes;
    mp_float_t  scale;
    uint32_t    fgcolor;
    uint32_t    bgcolor;
    uint8_t     vgacolor;
    uint8_t     rowspace;
    uint8_t     type;
    uint8_t     show_cursor;
    uint8_t     wrap;
    uint8_t     loaded;
} console_eve_obj_t;

typedef struct _list_eve_obj_t {
    mp_obj_base_t base;
    uint32_t addr;
    uint32_t size;
    uint8_t loaded;
} list_eve_obj_t;

typedef struct _ramg_objects_t {
    uint16_t count;
    uint16_t size;
    void **objects;
} ramg_objects_t;

const mp_obj_type_t display_eve_type;
const mp_obj_type_t font_eve_type;
const mp_obj_type_t image_eve_type;
const mp_obj_type_t list_eve_type;
const mp_obj_type_t console_eve_type;
const mp_obj_type_t tft_eve_type;

static display_eve_obj_t *eve_obj = NULL;
static font_eve_obj_t *user_fonts[MAX_USER_FONTS] = {NULL};
static ramg_objects_t ramg_objects = {0, 0, NULL};
static uint16_t eve_cmd_start_addr = 0;
static int16_t loaded_lists = 0;
static int16_t loaded_images = 0;
static int16_t loaded_fonts = 0;

extern uint8_t disp_used_spi_host;

static const char* const ft8_prim[] = {
    "BITMAPS",
    "POINTS",
    "LINES",
    "LINE_STRIP",
    "EDGE_STRIP_R",
    "EDGE_STRIP_L",
    "EDGE_STRIP_A",
    "EDGE_STRIP_B",
    "RECTS",
    "?",
};

static const char* const ft8_imgtypes[] = {
    "PNG",
    "JPEG",
    "RAW_COMPRESSED",
    "RAW",
};

static const char* const ft8_bmp_formats[] = {
    "ARGB1555",
    "L1",
    "L4",
    "L8",
    "RGB332",
    "ARGB2",
    "ARGB4",
    "RGB565",
    "PALETTED",
    "TEXT8X8",
    "TEXTVGA",
    "BARGRAPH",
};

static const char TAG[] = "[ModEve]";
static const uint8_t stride_factor[18] = {0, 4, 2, 1, 1, 1, 0, 0, 16, 1, 0, 1, 16, 16, 1, 1, 1, 3 };

//------------------------------------------------------------
static int _check_jpeg(uint8_t *data, int *width, int *height)
{
    int tmp;
    int idx = 0;
    tmp = (data[idx]<<8) | data[idx+1];
    if (tmp != 0xFFD8) return -1;
    idx += 2;
    tmp = (data[idx]<<8) | data[idx+1];
    if (tmp != 0xFFE0) return -2;
    idx += 2;
    tmp = (data[idx]<<8) | data[idx+1]; // size
    idx += 2;
    if (memcmp(data+idx, "JFIF", 4)) return -3;
    idx += (tmp-2);

    // segment
    do {
        if (data[idx] != 0xFF) return -4;
        tmp = (data[idx]<<8) | data[idx+1];
        if (tmp == 0xFFC0) break;
        idx += 2;
        tmp = (data[idx]<<8) | data[idx+1]; // size
        idx += tmp;
    } while (idx < (254));

    if (tmp != 0xFFC0) return -5;
    if ((idx+8) > 256) return -6;
    // get width and height
    idx += 4;
    if (data[idx] != 8) return -7;
    idx++;
    *height = (data[idx]<<8) | data[idx+1];
    idx += 2;
    *width = (data[idx]<<8) | data[idx+1];
    return 0;
}

#if FT8_ENABLE_PNG_LOADING
//------------------------------------------------------------
static int _check_png(uint8_t *data, int *width, int *height)
{
    int idx = 0;
    if (memcmp(data, "\x89PNG\x0d\x0a\x1a\x0a", 8)) return -1;
    idx += 12;
    if (memcmp(data+idx, "IHDR", 4)) return -2;
    idx += 6;
    *width = (data[idx] << 8) | data[idx+1];
    idx += 4;
    *height = (data[idx] << 8) | data[idx+1];
    idx += 2;
    if (data[idx] != 8) return -3; // only bit depth 8 is supported
    idx++;
    if (data[idx] == 4) return -4; // Grayscale and alpha not supported
    return data[idx];
}
#endif

#ifdef CONFIG_EVE_CHIP_TYPE1
//---------------------------------------------------------------------------------------
static int _check_avi(uint8_t *data, int *width, int *height, int* playtime, int *frames)
{
    int idx = 0;
    if (memcmp(data, "RIFF", 4)) return -1;
    idx = 8;
    if (memcmp(data+idx, "AVI ", 4)) return -2;
    if ((memcmp(data+108, "vidsMJPG", 8)) && (memcmp(data+108, "vidsmjpg", 8))) return -3;
    memcpy(width, data+64, 4);
    memcpy(height, data+68, 4);
    uint32_t sec_between_frames, total_frames;
    memcpy(&sec_between_frames, data+32, 4);
    memcpy(&total_frames, data+48, 4);
    uint64_t time = total_frames * sec_between_frames;
    *playtime = time / 1000000;
    *frames = total_frames;

    return 0;
}
#endif

//------------------------------------------------------
STATIC void spi_deinit_internal(display_eve_obj_t *self)
{
    if (self->disp_spi->handle) {
        esp_err_t ret;
        // Deinitialize display spi device(s)
        ret = remove_extspi_device(self->disp_spi);
        if (ret != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "Error removing display device");
        }

        gpio_pad_select_gpio(self->dconfig.miso);
        gpio_pad_select_gpio(self->dconfig.mosi);
        gpio_pad_select_gpio(self->dconfig.sck);

        eve_spibus_is_init = 0;
        spi_is_init = 0;
        eve_obj = NULL;
    }
}

//-----------------------------------
static bool check_ramg(uint32_t size)
{
    if ((ft8_ramg_ptr+size) > (FT8_RAM_G_SIZE)) return false;
    return true;
}

//------------------------------------
static bool add_ramg_object(void *obj)
{
    if (ramg_objects.count == ramg_objects.size) return false;

    void **obj_pos = ramg_objects.objects + (ramg_objects.count*sizeof(void*));
    *obj_pos = obj;
    ramg_objects.count++;
    obj_pos += sizeof(void*);
    if (ramg_objects.count == ramg_objects.size) {
        // add space for 16 new objects
        void **tmp_ptr = realloc(ramg_objects.objects, (ramg_objects.size+16)*sizeof(void*));
        if (tmp_ptr) {
            ramg_objects.objects = tmp_ptr;
            ramg_objects.size += 16;
            memset(obj_pos, 0, 16*sizeof(void*));
        }
    }
    return true;
}

//---------------------------------------------------------------------------
static void adjust_ramg_objects(void *this_obj, uint32_t addr, uint32_t size)
{
    void **obj_pos;
    // delete this object
    for (int i=0; i<ramg_objects.count; i++) {
        obj_pos = ramg_objects.objects + (sizeof(void*) * i);
        if (*obj_pos == this_obj) {
            // move the following objects
            int obj_after_count = ramg_objects.count-i+1;
            if (obj_after_count > 0)memcpy(obj_pos, obj_pos+sizeof(void*), sizeof(ramg_objects_t) * obj_after_count);
            // delete the last object
            memset(ramg_objects.objects + (sizeof(void*) * (ramg_objects.count-1)), 0, sizeof(ramg_objects_t));
            ramg_objects.count--;
            break;
        }
    }
    // adjust the RAM_G addresses of the remaining objects
    for (int i=0; i<ramg_objects.count; i++) {
        obj_pos = ramg_objects.objects + (sizeof(void*) * i); // get the object's pointer position
        void *ptr = *obj_pos;
        if (ptr != NULL) {
            mp_obj_base_t *base = (mp_obj_base_t *)ptr;
            if (base->type == &font_eve_type) {
                font_eve_obj_t *obj = (font_eve_obj_t *)ptr;
                if (obj->addr > addr) {
                    obj->addr -= size;
                    obj->metrics.ptr -= size;
                }
            }
            else if (base->type == &image_eve_type) {
                image_eve_obj_t *obj = (image_eve_obj_t *)ptr;
                if (obj->addr > addr) {
                    obj->addr -= size;
                }
            }
            else if (base->type == &list_eve_type) {
                list_eve_obj_t *obj = (list_eve_obj_t *)ptr;
                if (obj->addr > addr) {
                    obj->addr -= size;
                }
            }
            else if (base->type == &console_eve_type) {
                console_eve_obj_t *obj = (console_eve_obj_t *)ptr;
                if (obj->addr > addr) {
                    obj->addr -= size;
                }
            }
            #ifdef CONFIG_MICROPY_USE_TFT
            else if (base->type == &tft_eve_type) {
                tft_eve_obj_t *obj = (tft_eve_obj_t *)ptr;
                if (obj->addr > addr) {
                    obj->addr -= size;
                }
            }
            #endif
        }
    }
}

//------------------------------------------------
static void print_objects(const mp_print_t *print)
{
    if (ramg_objects.count == 0) return;
    void **obj_pos;
    for (int i=0; i<ramg_objects.count; i++) {
        obj_pos = ramg_objects.objects + (sizeof(void*) * i); // get the object's pointer position
        void *ptr = *obj_pos;
        if (ptr != NULL) {
            mp_obj_base_t *base = (mp_obj_base_t *)ptr;
            if (base->type == &font_eve_type) {
                font_eve_obj_t *obj = (font_eve_obj_t *)ptr;
                mp_printf(print, "        %2d: Font, addr=%u, size=%u\n", i, obj->addr,obj->size);
            }
            else if (base->type == &image_eve_type) {
                image_eve_obj_t *obj = (image_eve_obj_t *)ptr;
                mp_printf(print, "        %2d: Image, addr=%u, size=%u\n", i, obj->addr,obj->size);
            }
            else if (base->type == &list_eve_type) {
                list_eve_obj_t *obj = (list_eve_obj_t *)ptr;
                mp_printf(print, "        %2d: List, addr=%u, size=%u\n", i, obj->addr,obj->size);
            }
            else if (base->type == &console_eve_type) {
                console_eve_obj_t *obj = (console_eve_obj_t *)ptr;
                mp_printf(print, "        %2d: Console, addr=%u, size=%u\n", i, obj->addr,obj->size);
            }
            #ifdef CONFIG_MICROPY_USE_TFT
            else if (base->type == &tft_eve_type) {
                tft_eve_obj_t *obj = (tft_eve_obj_t *)ptr;
                mp_printf(print, "        %2d: Tft, addr=%u, size=%u\n", i, obj->addr,obj->size);
            }
            #endif
            else {
                mp_printf(print, "        %2d: Unknown object type\n", i);
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------
STATIC void display_eve_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    display_eve_obj_t *self = self_in;
    if (self->disp_spi->handle) {
        if (eve_chip_id > 0) {
            mp_printf(print, "EVE ( %dx%d, Type=FT%X, Ready: %s, Clk=%u Hz\n",
                    self->width, self->height, eve_chip_id, ((self->disp_spi->handle) ? "yes" : "no"), self->dconfig.speed);
            mp_printf(print, "      Pins: miso=%d, mosi=%d, clk=%d, cs=%d, pd=%d\n", self->dconfig.miso, self->dconfig.mosi, self->dconfig.sck, self->dconfig.cs, self->dconfig.pd);
            mp_printf(print, "      Free Objects RAM: %u (%u used)\n", FT8_RAM_G+FT8_RAM_G_SIZE-ft8_ramg_ptr, ft8_ramg_ptr);
            mp_printf(print, "      Loaded objects: fonts=%d, images=%d, lists=%d\n", loaded_fonts, loaded_images, loaded_lists);
            print_objects(print);
            mp_printf(print, "    )");
        }
        else {
            mp_printf(print, "EVE ( FT8xx not detected )");
        }
    }
    else {
        mp_printf(print, "EVE ( Not initialized )");
    }
}

//---------------------------------------------------------------------------------------------
STATIC void font_eve_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    font_eve_obj_t *self = self_in;
    if (self->loaded) {
        mp_printf(print, "EVE_FONT ( handle=%d )\n", self->handle);
        mp_printf(print, "          Format: L%d\n", self->format);
        mp_printf(print, "          Stride: %d\n", self->metrics.stride);
        mp_printf(print, "      Font width: %d\n", self->metrics.width);
        mp_printf(print, "     Font height: %d\n", self->metrics.height);
        mp_printf(print, "  Max char width: %d\n", self->maxwidth);
        mp_printf(print, "  Min char width: %d\n", self->minwidth);
        mp_printf(print, "   RAM_G address: %u\n", self->addr);
        mp_printf(print, "      RAM_G size: %u\n", self->size);
        mp_printf(print, "       Data addr: %d\n", self->metrics.ptr);
        mp_printf(print, " Number of chars: %d\n", self->nchars);
        mp_printf(print, "      First char: %d\n", self->firstc);
    }
    else {
        mp_printf(print, "EVE_FONT ( Unloaded )");
    }
}

//----------------------------------------------------------------------------------------------
STATIC void image_eve_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    image_eve_obj_t *self = self_in;
    if (self->loaded) {
        mp_printf(print, "EVE_IMAGE ( format=%s (from %s), addr=%u, size=%u, w=%d, h=%d )",
                ft8_bmp_formats[self->format], ft8_imgtypes[self->orig_fmt], self->addr, self->size, self->width, self->height);
    }
    else {
        mp_printf(print, "EVE_IMAGE ( Unloaded )");
    }
}

//---------------------------------------------------------------------------------------------
STATIC void list_eve_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    list_eve_obj_t *self = self_in;
    if (self->loaded) {
        mp_printf(print, "EVE_LIST ( addr=%u, size=%u )", self->addr, self->size);
    }
    else {
        mp_printf(print, "EVE_LIST ( Unloaded )");
    }
}

//------------------------------------------------------------------------------------------------
STATIC void console_eve_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    console_eve_obj_t *self = self_in;
    if (self->loaded) {
        mp_printf(print, "EVE_CONSOLE_%s [%d * %d] ( addr=%u, size=%u, cursor: (%d,%d), row space=%d, scale=%1.2f )",
                (self->type == FT8_TEXT8X8) ? "8x8" : "VGA", self->width, self->height, self->addr, self->size, self->x, self->y, self->rowspace, self->scale);
    }
    else {
        mp_printf(print, "EVE_CONSOLE ( Unloaded )");
    }
}

#ifdef CONFIG_MICROPY_USE_TFT
//--------------------------------------------------------------------------------------------
STATIC void tft_eve_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    tft_eve_obj_t *self = self_in;
    if (self->loaded) {
        mp_printf(print, "EVE_TFT_%s [%d * %d] ( addr=%u, size=%u )",
                (self->type == FT8_RGB565) ? "RGB565" : "RGB332", self->width, self->height, self->addr, self->size);
    }
    else {
        mp_printf(print, "EVE_TFT ( Unloaded )");
    }
}
#endif

//--------------------------------------------
static esp_err_t _EVE_calibrate(bool nvs_read)
{
    uint32_t cal_const = 0;
    char calibs[16];
    esp_err_t err;

    if (nvs_read) {
        // Get calibration values from NVS
        if (mpy_nvs_handle == 0) err = ESP_FAIL;
        for (int i=0; i<6; i++) {
            sprintf(calibs, "eve_calib_%c", (char)(i+0x61));
            err = nvs_get_u32(mpy_nvs_handle, calibs, &cal_const);
            if (err != ESP_OK) break;
            FT8_memWrite32(REG_TOUCH_TRANSFORM_A + (i*4), cal_const);
        }
        return err;
    }

    // Perform calibration
    FT8_start_cmd_burst();
    FT8_cmd_dl(CMD_DLSTART);
    FT8_cmd_dl(DL_CLEAR_RGB | 0x00e0e0e0);
    FT8_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
    FT8_cmd_dl(DL_COLOR_RGB | 0x00000040);
    FT8_cmd_text(400, 240, 28, FT8_OPT_CENTERX, "Please tap on the dot");
    FT8_cmd_calibrate();

    FT8_cmd_dl(DL_DISPLAY);
    FT8_cmd_dl(CMD_SWAP);

    FT8_end_cmd_burst();

    bool res = FT8_cmd_execute(30000);

    FT8_start_cmd_burst();
    FT8_cmd_dl(CMD_DLSTART);
    FT8_cmd_dl(DL_CLEAR_RGB);
    FT8_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
    if (res) {
        FT8_cmd_dl(DL_COLOR_RGB | 0x0000FF00);
        FT8_cmd_text(400, 240, 28, FT8_OPT_CENTERX, "Calibration OK");
    }
    else {
        FT8_cmd_dl(DL_COLOR_RGB | 0x0000FF00);
        FT8_cmd_text(400, 240, 28, FT8_OPT_CENTERX, "Calibration failed");
    }
    FT8_cmd_dl(DL_DISPLAY);
    FT8_cmd_dl(CMD_SWAP);
    FT8_end_cmd_burst();
    FT8_cmd_execute(250);

    err = ESP_OK;
    if (res) {
        // Save calibration constants to NVS
        for (int i=0; i<6; i++) {
            cal_const = FT8_memRead32(REG_TOUCH_TRANSFORM_A + (i*4));
            sprintf(calibs, "eve_calib_%c", (char)(i+0x61));
            err = nvs_set_u32(mpy_nvs_handle, calibs, cal_const);
            if (err != ESP_OK) break;
            nvs_commit(mpy_nvs_handle);
        }
    }

    return err;
}

#ifdef CONFIG_EVE_CHIP_TYPE1
//-------------------------------------------------------
static void _eve_rotate(display_eve_obj_t *self, int rot)
{
    FT8_start_cmd_burst();
    FT8_cmd_dl(CMD_DLSTART);
    FT8_cmd_setrotate(rot);
    FT8_cmd_dl(DL_DISPLAY);
    FT8_end_cmd_burst();
    FT8_cmd_execute(250);

    if ((rot & 2)) {
        // portrait, invert width/heigth
        self->width = self->dconfig.height;
        self->height = self->dconfig.width;
    }
    else {
        // landscape
        self->width = self->dconfig.width;
        self->height = self->dconfig.height;
    }
}
#endif

// === EVE object constructor ===
//---------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_eve_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    if (eve_obj) {
        mp_raise_msg(&mp_type_OSError, "EVE object instance already exists!");
    }
    display_eve_obj_t *self = m_new_obj(display_eve_obj_t);
    memset(self, 0, sizeof(display_eve_obj_t));
    self->base.type = &display_eve_type;
    self->spi = NULL;
    self->disp_spi_dev.handle = NULL;
    self->disp_spi_dev.cs = -1;
    self->disp_spi_dev.dc = -1;
    self->disp_spi_dev.selected = 0;
    self->disp_spi = &self->disp_spi_dev;
    eve_spi = NULL;
    memset(user_fonts, 0, sizeof(user_fonts));
    ft8_ramg_ptr = FT8_RAM_G;
    //list_ptr = EVE_STATIC_LIST;
    ramg_objects.count = 0;
    ramg_objects.size = 0;
    if (ramg_objects.objects == NULL) {
        ramg_objects.objects = malloc(16 * sizeof(uint32_t));
        if (ramg_objects.objects == NULL) {
            mp_raise_msg(&mp_type_OSError, "Error creating EVE instance");
        }
    }
    ramg_objects.size = 16;
    loaded_lists = 0;
    loaded_images = 0;
    eve_chip_id = 0;
    eve_obj = self;

    return MP_OBJ_FROM_PTR(self);
}

// ToDo: Change this
// --------------------
static void _eve_logo()
{
    FT8_start_cmd_burst();      // start writing to the cmd-fifo as one stream of bytes, only sending the address once
    FT8_cmd_dl(CMD_DLSTART);    // start the display list
    FT8_cmd_dl(CMD_LOGO);
    FT8_cmd_dl(DL_DISPLAY);     // End the display list. FT81X will ignore all the commands following this command.
    FT8_cmd_dl(CMD_SWAP);       // make this list active
    FT8_end_cmd_burst();        // stop writing to the cmd-fifo
    FT8_cmd_start();
    vTaskDelay(2500 / portTICK_RATE_MS);

    // Wait till both read & write pointer register are equal to zero
    uint16_t wr, rd;
    do {
        wr = FT8_memRead16(REG_CMD_WRITE);
        rd = FT8_memRead16(REG_CMD_READ);
    } while ((wr != 0) && (wr != rd));
    FT8_get_cmdoffset();
}

//-------------------------------------------------------------------------------------------------
STATIC mp_obj_t display_eve_config(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_hsize, ARG_vsize, ARG_vsync0, ARG_vsync1, ARG_voffset, ARG_vcycle, ARG_hsync0, ARG_hsync1, ARG_hoffset, ARG_hcycle, ARG_pclkpol, ARG_swizzle, ARG_pclk, ARG_cspread, ARG_rzthresh, ARG_hascrystal,ARG_hasgt911 };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_hsize,       MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 800 } },
        { MP_QSTR_vsize,       MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 480 } },
        { MP_QSTR_vsync0,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_vsync1,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 2 } },
        { MP_QSTR_voffset,     MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 13 } },
        { MP_QSTR_vcycle,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 525 } },
        { MP_QSTR_hsync0,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_hsync1,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 20 } },
        { MP_QSTR_hoffset,     MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 64 } },
        { MP_QSTR_hcycle,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 952 } },

        { MP_QSTR_pclkpol,     MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 1 } },
        { MP_QSTR_swizzle,     MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_pclk,        MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 2 } },
        { MP_QSTR_cspread,     MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_rzthresh,    MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 2000 } },
        { MP_QSTR_hascrystal,                    MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = true } },
        { MP_QSTR_hasgt911,                      MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = false } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];

    self->dconfig.disp_config.hsize = args[ARG_hsize].u_int;
    self->dconfig.disp_config.vsize = args[ARG_vsize].u_int;
    self->dconfig.disp_config.vsync0 = args[ARG_vsync0].u_int;
    self->dconfig.disp_config.vsync1 = args[ARG_vsync1].u_int;
    self->dconfig.disp_config.voffset = args[ARG_voffset].u_int;
    self->dconfig.disp_config.vcycle = args[ARG_vcycle].u_int;
    self->dconfig.disp_config.hsync0 = args[ARG_hsync0].u_int;
    self->dconfig.disp_config.hsync1 = args[ARG_hsync1].u_int;
    self->dconfig.disp_config.hoffset = args[ARG_hoffset].u_int;
    self->dconfig.disp_config.hcycle = args[ARG_hcycle].u_int;
    self->dconfig.disp_config.pclkpol = args[ARG_pclkpol].u_int;
    self->dconfig.disp_config.swizzle = args[ARG_swizzle].u_int;
    self->dconfig.disp_config.pclk = args[ARG_pclk].u_int;
    self->dconfig.disp_config.cspread = args[ARG_cspread].u_int;
    self->dconfig.disp_config.touch_thresh = args[ARG_rzthresh].u_int;
    self->dconfig.disp_config.has_crystal = args[ARG_hascrystal].u_bool;
    self->dconfig.disp_config.has_GT911 = args[ARG_hasgt911].u_bool;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_eve_config_obj, 0, display_eve_config);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t display_eve_init(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_host, ARG_width, ARG_height, ARG_speed, ARG_miso, ARG_mosi, ARG_clk, ARG_cs, ARG_pd, ARG_rot, ARG_splash };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_spihost,                     MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = HSPI_HOST } },
        { MP_QSTR_width,                       MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 800 } },
        { MP_QSTR_height,                      MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 480 } },
        { MP_QSTR_speed,                       MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = 10000000 } },
        { MP_QSTR_miso,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_mosi,      MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_clk,       MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_cs,        MP_ARG_REQUIRED | MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_pd,                          MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_rot,                         MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_splash,                      MP_ARG_KW_ONLY  | MP_ARG_BOOL, { .u_bool = false } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    esp_err_t ret;

    #ifdef CONFIG_FT8_USER_TYPE

    if ((self->dconfig.disp_config.hsize == 0) || (self->dconfig.disp_config.vsize == 0)) {
        mp_raise_ValueError("EVE display not configured");
    }

    #else

    #if defined (FT8_HAS_CRYSTAL)
    self->dconfig.disp_config.has_crystal = 1;
    #else
    self->dconfig.disp_config.has_crystal = 0;
    #endif
    #if defined (FT8_HAS_GT911)
    self->dconfig.disp_config.has_GT911 = 1;
    #else
    self->dconfig.disp_config.has_GT911 = 0;
    #endif
    self->dconfig.disp_config.cspread = FT8_CSPREAD;
    self->dconfig.disp_config.hcycle = FT8_HCYCLE;
    self->dconfig.disp_config.hoffset = FT8_HOFFSET;
    self->dconfig.disp_config.hsize = FT8_HSIZE;
    self->dconfig.disp_config.hsync0 = FT8_HSYNC0;
    self->dconfig.disp_config.hsync1 = FT8_HSYNC1;
    self->dconfig.disp_config.pclk = FT8_PCLK;
    self->dconfig.disp_config.pclkpol = FT8_PCLKPOL;
    self->dconfig.disp_config.swizzle = FT8_SWIZZLE;
    self->dconfig.disp_config.touch_thresh = FT8_TOUCH_RZTHRESH;
    self->dconfig.disp_config.vcycle = FT8_VCYCLE;
    self->dconfig.disp_config.voffset = FT8_VOFFSET;
    self->dconfig.disp_config.vsize = FT8_VSIZE;
    self->dconfig.disp_config.vsync0 = FT8_VSYNC0;
    self->dconfig.disp_config.vsync1 = FT8_VSYNC1;

    self->dconfig.height = FT8_HSIZE;
    self->dconfig.width = FT8_VSIZE;
    #endif

    // === deinitialize display spi device if it was initialized ===
    if (self->disp_spi->handle) spi_deinit_internal(self);

    // === Get arguments ===
    if ((args[ARG_host].u_int != HSPI_HOST) && (args[ARG_host].u_int != VSPI_HOST)) {
        mp_raise_ValueError("SPI host must be either HSPI(1) or VSPI(2)");
    }
    if ((SPIbus_configs[VSPI_HOST] == NULL) && (args[ARG_host].u_int == VSPI_HOST)) {
        mp_raise_ValueError("SPI host must be HSPI(1), VSPI(2) used by SPIRAM");
    }

    self->dconfig.host = args[ARG_host].u_int;
    self->dconfig.width = args[ARG_width].u_int;   // native display width
    self->dconfig.height = args[ARG_height].u_int; // native display height
    self->width = self->dconfig.width;
    self->height = self->dconfig.height;

    self->dconfig.pd = args[ARG_pd].u_int;

    self->dconfig.miso = args[ARG_miso].u_int;
    self->dconfig.mosi = args[ARG_mosi].u_int;
    self->dconfig.sck = args[ARG_clk].u_int;

    self->dconfig.cs = args[ARG_cs].u_int;

    // ================================
    // ==== Initialize the Display ====
    ret = FT8_init(&self->dconfig, self->disp_spi);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, "Error initializing display");
    }

    disp_used_spi_host = args[ARG_host].u_int;

    // ==== Set SPI clock used for display operations ====
    self->dconfig.speed = spi_set_speed(self->disp_spi, args[ARG_speed].u_int);

    if (args[ARG_splash].u_bool) {
        _eve_logo();
    }

    ret = _EVE_calibrate(true);
    if (ret != ESP_OK) ret = _EVE_calibrate(false);

    if (ret == ESP_OK) {
        #ifdef CONFIG_EVE_CHIP_TYPE1
        if (args[ARG_rot].u_int >= 0) {
            _eve_rotate(self, args[ARG_rot].u_int & 7);
        }
        #endif
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_eve_init_obj, 0, display_eve_init);


// Check if EVE object is initialized
// and if the functions can be executed in current context
//----------------------------------------------------------
static void _check_inlist(display_eve_obj_t *self, uint8_t f)
{
    if ((eve_obj == NULL) || (eve_chip_id == 0) || (self->disp_spi->handle == NULL)) {
        mp_raise_msg(&mp_type_OSError, "EVE object not initialized!");
    }
    if ((f == 0) && (self->in_list)) {
        mp_raise_ValueError("Can only be used outside display list");
    }
    else if ((f == 1) && (self->in_list == 0)) {
        mp_raise_ValueError("Display list not started");
    }
}

// ---------------------------------------------------------------
STATIC mp_obj_t EVE_calibrate(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 0);

    esp_err_t err;

    if ((n_args > 1) && (mp_obj_is_true(args[1]))) {
        err = _EVE_calibrate(true);
        if (err == ESP_OK) return mp_const_true;
        return mp_const_false;
    }

    err = _EVE_calibrate(false);

    if (err == ESP_OK) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_calibrate_obj, 1, 2, EVE_calibrate);


#ifdef CONFIG_EVE_CHIP_TYPE1
// ----------------------------------------------------------
STATIC mp_obj_t EVE_rotate(mp_obj_t self_in, mp_obj_t rot_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    int rot = mp_obj_get_int(rot_in);
    if ((rot < 0) || (rot > 7)) {
        mp_raise_ValueError("Invalid rotation value");
    }
    _eve_rotate(self, rot);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_rotate_obj, EVE_rotate);
#endif

// Start creating display list
// --------------------------------------------
STATIC mp_obj_t EVE_startlist(mp_obj_t self_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);
    FT8_CP_reset();
    FT8_memWrite32(REG_CMD_DL, 0);

    eve_cmd_start_addr = eve_cmdOffset;
    FT8_start_cmd_burst();      // start writing to the cmd-fifo as one stream of bytes, only sending the address once
    FT8_cmd_dl(CMD_DLSTART);    // start the display list
    //FT8_cmd_dl(CMD_COLDSTART);
    self->in_list = 1;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_startlist_obj, EVE_startlist);

// End display list, make it active and show on display
// -----------------------------------------
STATIC mp_obj_t EVE_endlist(mp_obj_t self_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    FT8_cmd_dl(DL_DISPLAY); // End the display list. FT81X will ignore all the commands following this command.
    FT8_cmd_dl(CMD_SWAP);   // make this list active

    FT8_end_cmd_burst(); // stop writing to the cmd-fifo
    bool res = FT8_cmd_execute(250);
    self->in_list = 0;

    if (res) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_endlist_obj, EVE_endlist);

// End the current list and save it to static DL RAM
// Returns List object
// -------------------------------------------
STATIC mp_obj_t EVE_savelist(mp_obj_t self_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    // Execute current list
    FT8_end_cmd_burst(); // stop writing to the cmd-fifo
    bool res = FT8_cmd_execute(250);
    self->in_list = 0;
    if (!res) return mp_const_false;

    // Copy to static list
    uint32_t list_size = FT8_memRead32(REG_CMD_DL) & 0x1FFF;
    if (!check_ramg(list_size)) {
        mp_raise_ValueError("No place to append list");
    }
    FT8_cmd_memcpy(ft8_ramg_ptr, FT8_RAM_DL, list_size);
    res = FT8_cmd_execute(250);
    if (!res) return mp_const_false;

    // Create LIST instance object
    list_eve_obj_t *list_obj = m_new_obj(list_eve_obj_t);
    memset(list_obj, 0, sizeof(list_eve_obj_t));
    list_obj->base.type = &list_eve_type;

    list_obj->addr = ft8_ramg_ptr;
    list_obj->size = list_size;
    list_obj->loaded = 1;

    if (!add_ramg_object((void *)list_obj)) {
        mp_raise_ValueError("Error adding ramg object");
    }
    ft8_ramg_ptr += list_size;
    loaded_lists++;

    return MP_OBJ_FROM_PTR(list_obj);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_savelist_obj, EVE_savelist);


// ---------------------------------------------------------------
STATIC mp_obj_t EVE_appendlist(mp_obj_t self_in, mp_obj_t list_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    if (mp_obj_get_type(list_in) != &list_eve_type) {
        mp_raise_ValueError("List object expected");
    }

    list_eve_obj_t *list = (list_eve_obj_t *)(list_in);
    if (list->loaded == 0) {
        mp_raise_ValueError("List unloaded");
    }

    FT8_cmd_append(list->addr, list->size);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_appendlist_obj, EVE_appendlist);

// -----------------------------------------------------------
STATIC mp_obj_t EVE_clear(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);
    uint32_t color = 0;
    uint8_t alpha = 0;
    if (n_args > 1) color = mp_obj_get_int(args[1]) & 0x00ffffff;
    if (n_args > 2) alpha = mp_obj_get_int(args[2]) & 0xff;
    FT8_cmd_dl(DL_CLEAR_RGB | color);
    FT8_cmd_dl(CLEAR_COLOR_A(alpha));
    // clear the screen - this and the previous prevent artifacts between lists.
    // Attributes are the color, stencil and tag buffers
    FT8_cmd_dl(CLEAR(1,1,1));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_clear_obj, 1, 3, EVE_clear);

// -----------------------------------------------------------
STATIC mp_obj_t EVE_color(mp_obj_t self_in, mp_obj_t color_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    mp_int_t color = mp_obj_get_int(color_in);
    FT8_cmd_dl(DL_COLOR_RGB | ((uint32_t)color & 0x00ffffff));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_color_obj, EVE_color);

// -------------------------------------------------------------
STATIC mp_obj_t EVE_fgcolor(mp_obj_t self_in, mp_obj_t color_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    mp_int_t color = mp_obj_get_int(color_in);
    FT8_cmd_fgcolor((uint32_t)color & 0x00ffffff);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_fgcolor_obj, EVE_fgcolor);

// -------------------------------------------------------------
STATIC mp_obj_t EVE_bgcolor(mp_obj_t self_in, mp_obj_t color_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    mp_int_t color = mp_obj_get_int(color_in);
    FT8_cmd_bgcolor((uint32_t)color & 0x00ffffff);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_bgcolor_obj, EVE_bgcolor);

// -----------------------------------------------------------
STATIC mp_obj_t EVE_alpha(mp_obj_t self_in, mp_obj_t alpha_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    int alpha = mp_obj_get_int(alpha_in) & 0xFF;
    FT8_cmd_dl(COLOR_A((uint8_t)alpha));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_alpha_obj, EVE_alpha);

// ---------------------------------------------------------------
STATIC mp_obj_t EVE_alphafunc(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint8_t func = mp_obj_get_int(args[1]) & 7;
    uint8_t ref = 0;
    if (n_args > 2) ref = mp_obj_get_int(args[2]) & 255;

    FT8_cmd_dl(ALPHA_FUNC(func, ref));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_alphafunc_obj, 2, 3, EVE_alphafunc);

// -----------------------------------------------------------------
STATIC mp_obj_t EVE_stencilfunc(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint8_t func = mp_obj_get_int(args[1]) & 7;
    uint8_t mask = mp_obj_get_int(args[2]) & 255;
    uint8_t ref = 0;
    if (n_args > 3) ref = mp_obj_get_int(args[3]) & 255;

    FT8_cmd_dl(STENCIL_FUNC(func, ref, mask));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_stencilfunc_obj, 3, 4, EVE_stencilfunc);

// ----------------------------------------------------------------
STATIC mp_obj_t EVE_stencilmask(mp_obj_t self_in, mp_obj_t mask_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);

    uint8_t mask = mp_obj_get_int(mask_in) & 255;

    FT8_cmd_dl(STENCIL_MASK(mask));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_stencilmask_obj, EVE_stencilmask);

// ----------------------------------------------------------------------------------
STATIC mp_obj_t EVE_stencilop(mp_obj_t self_in, mp_obj_t sfail_in, mp_obj_t spass_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);

    uint8_t sfail = mp_obj_get_int(sfail_in) & 7;
    uint8_t spass = mp_obj_get_int(sfail_in) & 7;

    FT8_cmd_dl(STENCIL_OP(sfail, spass));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(EVE_stencilop_obj, EVE_stencilop);

// ---------------------------------------------------------------
STATIC mp_obj_t EVE_colormask(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    FT8_cmd_dl(COLOR_MASK(mp_obj_get_int(args[1]), mp_obj_get_int(args[2]), mp_obj_get_int(args[3]), mp_obj_get_int(args[4])));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_colormask_obj, 5, 5, EVE_colormask);

// -----------------------------------------------------------
STATIC mp_obj_t EVE_blend(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    FT8_cmd_dl(BLEND_FUNC(mp_obj_get_int(args[1]), mp_obj_get_int(args[2])));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_blend_obj, 3, 3, EVE_blend);

// -------------------------------------------------------------
STATIC mp_obj_t EVE_backlight(mp_obj_t self_in, mp_obj_t bkl_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);
    int bkl = mp_obj_get_int(bkl_in) & 0x7F;
    FT8_memWrite8(REG_PWM_DUTY, bkl);   // set the backlight

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_backlight_obj, EVE_backlight);

// ----------------------------------------------------------------------
STATIC mp_obj_t EVE_scale(mp_obj_t self_in, mp_obj_t x_in, mp_obj_t y_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);

    float x = mp_obj_get_float(x_in);
    float y = mp_obj_get_float(y_in);
    int32_t ix = (uint32_t)(x * 65536);
    int32_t iy = (uint32_t)(y * 65536);

    FT8_cmd_scale(ix, iy);
    FT8_cmd_setmatrix();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(EVE_scale_obj, EVE_scale);

// -----------------------------------------------------------
STATIC mp_obj_t EVE_point(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t r = mp_obj_get_int(args[3]);

    FT8_cmd_point(x, y, r);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_point_obj, 4, 4, EVE_point);

// ------------------------------------------------------------
STATIC mp_obj_t EVE_circle(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t r = mp_obj_get_int(args[3]);
    uint32_t color = mp_obj_get_int(args[4]) & 0x00ffffff;
    uint16_t lsize = 1;
    if (n_args > 5) lsize = mp_obj_get_int(args[5]);

    // draw to alpha buffer
    FT8_cmd_dl(COLOR_MASK(0, 0, 0, 1));
    FT8_cmd_dl(BLEND_FUNC(FT8_ONE, FT8_ONE_MINUS_SRC_ALPHA));
    FT8_cmd_point(x, y, r); // outer circle
    FT8_cmd_dl(BLEND_FUNC(FT8_ZERO, FT8_ONE_MINUS_SRC_ALPHA));
    FT8_cmd_point(x, y, r-lsize); // inner circle

    // Draw outher circle from alpha buffer
    FT8_cmd_dl(COLOR_MASK(1, 1, 1, 0));
    FT8_cmd_dl(BLEND_FUNC(FT8_DST_ALPHA, FT8_ONE));
    FT8_cmd_dl(DL_COLOR_RGB | color);
    //FT8_cmd_rect(x-r, y-r, x+r, y+r, 1);
    FT8_cmd_point(x, y, r); // outer circle

    //FT8_cmd_dl(COLOR_MASK(1, 1, 1, 1));
    FT8_cmd_dl(BLEND_FUNC(FT8_SRC_ALPHA, FT8_ONE_MINUS_SRC_ALPHA));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_circle_obj, 5, 6, EVE_circle);

// ----------------------------------------------------------
STATIC mp_obj_t EVE_line(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t x0 = mp_obj_get_int(args[1]);
    uint16_t y0 = mp_obj_get_int(args[2]);
    uint16_t x1 = mp_obj_get_int(args[3]);
    uint16_t y1 = mp_obj_get_int(args[4]);
    uint16_t width = mp_obj_get_int(args[5]) & 255;

    FT8_cmd_line(x0, y0, x1, y1, width);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_line_obj, 6, 6, EVE_line);

// ----------------------------------------------------------
STATIC mp_obj_t EVE_rect(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t x0 = mp_obj_get_int(args[1]);
    uint16_t y0 = mp_obj_get_int(args[2]);
    uint16_t x1 = mp_obj_get_int(args[3]);
    uint16_t y1 = mp_obj_get_int(args[4]);
    uint16_t corner = 1;
    if (n_args > 5) corner = mp_obj_get_int(args[5]) & 255;

    FT8_cmd_rect(x0, y0, x1, y1, corner);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_rect_obj, 5, 6, EVE_rect);

// ---------------------------------------------------------------
STATIC mp_obj_t EVE_rectangle(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t lwidth = 1;
    if (n_args > 5) lwidth = mp_obj_get_int(args[5]);
    uint16_t data[10];
    uint16_t x0 = mp_obj_get_int(args[1]);
    uint16_t y0 = mp_obj_get_int(args[2]);
    uint16_t x1 = mp_obj_get_int(args[3]);
    uint16_t y1 = mp_obj_get_int(args[4]);
    data[0] = x0;
    data[1] = y0;
    data[2] = x1;
    data[3] = y0;
    data[4] = x1;
    data[5] = y1;
    data[6] = x0;
    data[7] = y1;
    data[8] = x0;
    data[9] = y0;

    FT8_cmd_strip(data, 10, FT8_LINE_STRIP, lwidth);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_rectangle_obj, 5, 6, EVE_rectangle);

// --------------------------------------------------------------
STATIC mp_obj_t EVE_triangle(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t lwidth = 1;
    if (n_args > 7) lwidth = mp_obj_get_int(args[7]);
    uint16_t data[8];
    data[0] = mp_obj_get_int(args[1]);
    data[1] = mp_obj_get_int(args[2]);
    data[2] = mp_obj_get_int(args[3]);
    data[3] = mp_obj_get_int(args[4]);
    data[4] = mp_obj_get_int(args[5]);
    data[5] = mp_obj_get_int(args[6]);
    data[6] = mp_obj_get_int(args[1]);
    data[7] = mp_obj_get_int(args[2]);

    FT8_cmd_strip(data, 8, FT8_LINE_STRIP, lwidth);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_triangle_obj, 7, 8, EVE_triangle);

// ------------------------------------------------------------
STATIC mp_obj_t EVE_strips(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t lwidth = 1;
    if (n_args > 3) lwidth = mp_obj_get_int(args[3]);

    uint16_t type = mp_obj_get_int(args[1]);

    if (!MP_OBJ_IS_TYPE(args[2], &mp_type_array)) {
        mp_raise_ValueError("array argument expected");
    }
    mp_obj_array_t * arr = (mp_obj_array_t *)MP_OBJ_TO_PTR(args[2]);
    if ((arr->typecode != 'h') && (arr->typecode == 'H')) {
        mp_raise_ValueError("array argument of type 'h', 'H' or 'B' expected");
    }
    if ((arr->len < 2) || ((arr->len % 2) != 0)) {
        mp_raise_ValueError("array argument length must be even and >= 2");
    }
    uint16_t *buff = (uint16_t *)arr->items;

    FT8_cmd_strip(buff, arr->len*2, type, lwidth);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_strips_obj, 3, 4, EVE_strips);

// -----------------------------------------------------------------
STATIC mp_obj_t EVE_scissorSize(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    int16_t x = -1;
    int16_t y = -1;

    uint16_t w = mp_obj_get_int(args[1]);
    uint16_t h = mp_obj_get_int(args[2]);
    if (n_args == 5) {
        x = mp_obj_get_int(args[3]);
        y = mp_obj_get_int(args[4]);
    }

    FT8_cmd_dl(SCISSOR_SIZE(w, h));
    if ((x >= 0) && (y >= 0)) FT8_cmd_dl(SCISSOR_XY(x, y));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_scissorSize_obj, 3, 5, EVE_scissorSize);

// ---------------------------------------------------------------
STATIC mp_obj_t EVE_scissorXY(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 1);

    uint16_t x = mp_obj_get_int(args[3]);
    uint16_t y = mp_obj_get_int(args[4]);

    FT8_cmd_dl(SCISSOR_XY(x, y));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_scissorXY_obj, 3, 3, EVE_scissorXY);

// --------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_text(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_text, ARG_font, ARG_align };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_text,         MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_font,                           MP_ARG_INT, { .u_int = 20 } },
        { MP_QSTR_align,                          MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    #ifdef CONFIG_EVE_CHIP_TYPE1
    int max_font = 34;
    #else
    int max_font = 31;
    #endif
    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t font = args[ARG_font].u_int;
    uint32_t align = (uint32_t)args[ARG_align].u_int;
    if ((font < 0) || (font > max_font)) {
        mp_raise_ValueError("Unsupported font size");
    }
    uint32_t allowed_opts = FT8_OPT_CENTERX | FT8_OPT_CENTERY | FT8_OPT_CENTER | FT8_OPT_RIGHTX;
    if ((align & allowed_opts) != align) {
        mp_raise_ValueError("Unsupported text align");
    }
    const char *st = mp_obj_str_get_str(args[ARG_text].u_obj);

    if (font > 31) {
        #ifdef CONFIG_EVE_CHIP_TYPE1
        FT8_cmd_romfont(1, font);
        font = 1;
        #endif
    }
    FT8_cmd_text(x, y, font, align, st);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_text_obj, 0, EVE_text);

// --------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_number(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_num, ARG_font, ARG_align, ARG_base };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_num,          MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_font,                           MP_ARG_INT, { .u_int = 20 } },
        { MP_QSTR_align,                          MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_base,                           MP_ARG_INT, { .u_int = -1 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    #ifdef CONFIG_EVE_CHIP_TYPE1
    int max_font = 34;
    #else
    int max_font = 31;
    #endif
    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t font = args[ARG_font].u_int;
    uint32_t align = (uint32_t)args[ARG_align].u_int;
    if ((font < 0) || (font > max_font)) {
        mp_raise_ValueError("Unsupported font size");
    }
    uint32_t allowed_opts = FT8_OPT_CENTERX | FT8_OPT_CENTERY | FT8_OPT_CENTER | FT8_OPT_RIGHTX | FT8_OPT_SIGNED;
    if ((align & allowed_opts) != align) {
        mp_raise_ValueError("Unsupported number align");
    }
    int32_t num = mp_obj_get_int(args[ARG_num].u_obj);
    if (num < 0) align |= FT8_OPT_SIGNED;
    if (font > 31) {
        #ifdef CONFIG_EVE_CHIP_TYPE1
        FT8_cmd_romfont(1, font);
        font = 1;
        #endif
    }

    #ifdef CONFIG_EVE_CHIP_TYPE1
    int base = 10;
    if ((args[ARG_base].u_int > 1) && (args[ARG_base].u_int < 37)) {
        base = args[ARG_base].u_int;
        FT8_cmd_setbase(base);
    }
    #endif
    FT8_cmd_number(x, y, font, align, num);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_number_obj, 0, EVE_number);

// ----------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_button(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_width, ARG_height, ARG_text, ARG_font, ARG_opt };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height,       MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_text,         MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_font,                           MP_ARG_INT, { .u_int = 20 } },
        { MP_QSTR_opt,                            MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t width = args[ARG_width].u_int;
    mp_int_t height = args[ARG_height].u_int;
    mp_int_t font = args[ARG_font].u_int;
    mp_int_t opt = args[ARG_opt].u_int;
    if ((font < 0) || (font > 31)) {
        mp_raise_ValueError("Unsupported font size");
    }
    if ((opt != 0) && (opt != FT8_OPT_FLAT)) {
        mp_raise_ValueError("Unsupported option");
    }
    const char *st = mp_obj_str_get_str(args[ARG_text].u_obj);

    FT8_cmd_button(x, y, width, height, font, opt, st);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_button_obj, 0, EVE_button);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_toggle(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_x, ARG_y, ARG_width, ARG_state, ARG_text, ARG_font, ARG_opt };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_state,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_text,         MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_font,                           MP_ARG_INT, { .u_int = 20 } },
        { MP_QSTR_opt,                            MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t width = args[ARG_width].u_int;
    mp_int_t state = args[ARG_state].u_int;
    if (state) state = 65535;
    mp_int_t font = args[ARG_font].u_int;
    mp_int_t opt = args[ARG_opt].u_int;
    if ((font < 0) || (font > 31)) {
        mp_raise_ValueError("Unsupported font size");
    }
    if ((opt != 0) && (opt != FT8_OPT_FLAT)) {
        mp_raise_ValueError("Unsupported option");
    }
    const char *st = mp_obj_str_get_str(args[ARG_text].u_obj);

    FT8_cmd_toggle(x, y, width, font, opt, state, st);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_toggle_obj, 0, EVE_toggle);

//------------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_scrollbar(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_x, ARG_y, ARG_width, ARG_height, ARG_value, ARG_size, ARG_range, ARG_opt };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height,       MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_value,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_size,                           MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_range,                          MP_ARG_INT, { .u_int = 100 } },
        { MP_QSTR_opt,                            MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t width = args[ARG_width].u_int;
    mp_int_t height = args[ARG_height].u_int;
    mp_int_t value = args[ARG_value].u_int;
    mp_int_t size = args[ARG_size].u_int;
    if (size < 0) size = width / 5;
    mp_int_t range = args[ARG_range].u_int;
    mp_int_t opt = args[ARG_opt].u_int;
    if ((opt != 0) && (opt != FT8_OPT_FLAT)) {
        mp_raise_ValueError("Unsupported option");
    }

    FT8_cmd_scrollbar(x, y, width, height, opt, value, size, range);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_scrollbar_obj, 0, EVE_scrollbar);

//----------------------------------------------------------------------------------------------------------
STATIC void EVE_slider_progress(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args, uint8_t type)
{
    enum { ARG_x, ARG_y, ARG_width, ARG_height, ARG_value, ARG_range, ARG_opt };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height,       MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_value,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_range,                          MP_ARG_INT, { .u_int = 100 } },
        { MP_QSTR_opt,                            MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t width = args[ARG_width].u_int;
    mp_int_t height = args[ARG_height].u_int;
    mp_int_t value = args[ARG_value].u_int;
    mp_int_t range = args[ARG_range].u_int;
    mp_int_t opt = args[ARG_opt].u_int;
    if ((opt != 0) && (opt != FT8_OPT_FLAT)) {
        mp_raise_ValueError("Unsupported option");
    }

    if (type) FT8_cmd_progress(x, y, width, height, opt, value, range);
    else FT8_cmd_slider(x, y, width, height, opt, value, range);
}

//---------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_slider(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    EVE_slider_progress(n_args, pos_args, kw_args, 0);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_slider_obj, 0, EVE_slider);

//-----------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_progress(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    EVE_slider_progress(n_args, pos_args, kw_args, 1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_progress_obj, 0, EVE_progress);

//-------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_keys(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_x, ARG_y, ARG_width, ARG_height, ARG_labels, ARG_font, ARG_opt };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height,       MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_labels,       MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_font,                           MP_ARG_INT, { .u_int = 20 } },
        { MP_QSTR_opt,                            MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t width = args[ARG_width].u_int;
    mp_int_t height = args[ARG_height].u_int;
    mp_int_t font = args[ARG_font].u_int;
    mp_int_t opt = args[ARG_opt].u_int;
    const char *labels = mp_obj_str_get_str(args[ARG_labels].u_obj);
    if ((font < 0) || (font > 31)) {
        mp_raise_ValueError("Unsupported font size");
    }
    if ((opt >= 32) && (opt < 128)) {
        if (strchr(labels, opt) == NULL) {
            mp_raise_ValueError("Unsupported option");
        }
    }
    else {
        uint32_t allowed_opts = FT8_OPT_CENTERX | FT8_OPT_CENTERY | FT8_OPT_CENTER | FT8_OPT_RIGHTX | FT8_OPT_FLAT;
        if ((opt & allowed_opts) != opt) {
            mp_raise_ValueError("Unsupported option");
        }
    }

    FT8_cmd_keys(x, y, width, height, font, opt, labels);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_keys_obj, 0, EVE_keys);

// ---------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_clock(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_r, ARG_time, ARG_opt };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_r,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_time,                           MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_opt,                            MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t r = args[ARG_r].u_int;
    uint32_t opt = (uint32_t)args[ARG_opt].u_int;
    uint32_t allowed_opts = FT8_OPT_FLAT | FT8_OPT_NOBACK | FT8_OPT_NOTICKS | FT8_OPT_NOPOINTER | FT8_OPT_NOSECS | FT8_OPT_NOHANDS;
    if ((opt & allowed_opts) != opt) {
        mp_raise_ValueError("Unsupported option");
    }
    struct tm *tm_info;
    struct tm tm_inf;

    if (args[ARG_time].u_obj != mp_const_none) {
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
        tm_info = localtime(&seconds);
    }

    FT8_cmd_clock(x, y, r, opt, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, 0);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_clock_obj, 0, EVE_clock);

// ---------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_gauge(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_r, ARG_major, ARG_minor, ARG_range, ARG_val, ARG_opt };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_r,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_major,                          MP_ARG_INT, { .u_int = 10 } },
        { MP_QSTR_minor,                          MP_ARG_INT, { .u_int = 2 } },
        { MP_QSTR_range,                          MP_ARG_INT, { .u_int = 100 } },
        { MP_QSTR_val,                            MP_ARG_INT, { .u_int = 50 } },
        { MP_QSTR_opt,                            MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    mp_int_t x = args[ARG_x].u_int;
    mp_int_t y = args[ARG_y].u_int;
    mp_int_t r = args[ARG_r].u_int;
    mp_int_t major = args[ARG_major].u_int;
    mp_int_t minor = args[ARG_minor].u_int;
    mp_int_t val = args[ARG_val].u_int;
    mp_int_t range = args[ARG_range].u_int;
    uint32_t opt = (uint32_t)args[ARG_opt].u_int;
    uint32_t allowed_opts = FT8_OPT_FLAT | FT8_OPT_NOBACK | FT8_OPT_NOTICKS | FT8_OPT_NOPOINTER ;
    if ((opt & allowed_opts) != opt) {
        mp_raise_ValueError("Unsupported option");
    }

    FT8_cmd_gauge(x, y, r, opt, major, minor, val, range);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_gauge_obj, 0, EVE_gauge);

// ------------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_gradient(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x0, ARG_y0, ARG_rgb0, ARG_x1, ARG_y1, ARG_rgb1 };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x0,           MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y0,           MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_rgb0,         MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_x1,           MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y1,           MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_rgb1,         MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    int16_t x0 = args[ARG_x0].u_int;
    int16_t y0 = args[ARG_y0].u_int;
    uint32_t rgb0 = args[ARG_rgb0].u_int;
    int16_t x1 = args[ARG_x1].u_int;
    int16_t y1 = args[ARG_y1].u_int;
    uint32_t rgb1 = args[ARG_rgb1].u_int;

    FT8_cmd_gradient(x0, y0, rgb0, x1, y1, rgb1);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_gradient_obj, 0, EVE_gradient);

// ---------------------------------------------
STATIC mp_obj_t EVE_getprops(mp_obj_t self_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    uint32_t w, h, ptr;
    uint16_t offset = FT8_cmd_getprops(0);
    FT8_cmd_execute(250);
    w = FT8_memRead32(FT8_RAM_CMD + offset);
    offset += 4;
    offset &= 0x0fff;
    h = FT8_memRead32(FT8_RAM_CMD + offset);
    offset += 4;
    offset &= 0x0fff;
    ptr = FT8_memRead32(FT8_RAM_CMD + offset);

    mp_obj_t tuple[3];
    tuple[0] = mp_obj_new_int(w);
    tuple[1] = mp_obj_new_int(h);
    tuple[2] = mp_obj_new_int(ptr);

    return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_getprops_obj, EVE_getprops);

// -------------------------------------------------------------
STATIC mp_obj_t EVE_fontinfo(mp_obj_t self_in, mp_obj_t font_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    if (mp_obj_get_type(font_in) == &font_eve_type) {
        font_eve_printinfo(&mp_plat_print, font_in, PRINT_STR);
        return mp_const_none;
    }

    int fhandle = mp_obj_get_int(font_in);
    if ((fhandle < MAX_USER_FONTS) && user_fonts[fhandle] != NULL) {
        font_eve_printinfo(&mp_plat_print, user_fonts[fhandle], PRINT_STR);
        return mp_const_none;
    }

    // === ROM font, get metrics and print the font properties
    if ((fhandle < 16) || (fhandle > ((eve_chip_id < 0x810) ? 31:34))) {
        mp_raise_ValueError("Invalid font handle");
    }

    font_eve_obj_t font;
    font.base.type = &font_eve_type;
    // get the ROM fonts start address, fonts table starts at font 16
    uint32_t fp = FT8_memRead32(FT8_ROM_FONT_ADDR);
    // calculate the address of the requested font
    font.addr = fp + (EVE_FONT_METRICS_SIZE * (fhandle - 16));

    // read the font metrics
    FT8_memRead_buffer(font.addr, (uint8_t *)(&font.metrics.widths[0]), EVE_FONT_METRICS_SIZE);

    uint32_t fformat = font.metrics.format;
    if (fformat == 17) fformat = 2;
    else if (fformat == 2) fformat = 4;
    else if (fformat == 3) fformat = 8;
    font.minwidth = 99;
    font.maxwidth = 0;
    font.nchars = 0;
    for (int i=0; i<EVE_FONT_WIDTHS_SIZE; i++) {
        if (font.metrics.widths[i] > 0) {
            font.nchars++;
            if (font.firstc == 0) font.firstc = i;
            if (font.metrics.widths[i] < font.minwidth) font.minwidth = font.metrics.widths[i];
            if (font.metrics.widths[i] > font.maxwidth) font.maxwidth = font.metrics.widths[i];
        }
    }
    font.handle = fhandle;
    font.format = (uint8_t)fformat;

    font_eve_printinfo(&mp_plat_print, (mp_obj_t)&font, PRINT_STR);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_fontinfo_obj, EVE_fontinfo);

// -------------------------------------------------------------
STATIC mp_obj_t EVE_userfont(mp_obj_t self_in, mp_obj_t font_in)
{
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);
    if (mp_obj_get_type(font_in) != &font_eve_type) {
        mp_raise_ValueError("Font object expected");
    }

    font_eve_obj_t *font = (font_eve_obj_t *)(font_in);
    if (font->loaded == 0) {
        mp_raise_ValueError("Font unloaded");
    }

    #ifdef CONFIG_EVE_CHIP_TYPE1
    // For FT81x use CMD_SETFONT2
    FT8_cmd_setbitmap(font->metrics.ptr, font->metrics.format, font->metrics.width, font->metrics.height);
    FT8_cmd_setfont2(font->handle, font->addr, font->firstc);
    #else
    // For FT80x use CMD_SETFONT
    FT8_cmd_dl(BEGIN(FT8_BITMAPS));
    FT8_cmd_dl(BITMAP_HANDLE(font->handle));
    FT8_cmd_dl(BITMAP_SOURCE(font->metrics.ptr));
    FT8_cmd_dl(BITMAP_LAYOUT(font->metrics.format, font->metrics.stride, font->metrics.height));
    FT8_cmd_dl(BITMAP_SIZE(FT8_NEAREST, FT8_BORDER, FT8_BORDER, font->metrics.width, font->metrics.height));
    FT8_cmd_setfont(font->handle, font->addr);
    #endif

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_userfont_obj, EVE_userfont);

// -------------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_setbitmap(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_x, ARG_y, ARG_width, ARG_height, ARG_format, ARG_wrapx, ARG_wrapy, ARG_filter, ARG_scalex, ARG_scaley };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,         MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_width,        MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_height,       MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_format,                         MP_ARG_INT, { .u_int = FT8_RGB565 } },
        { MP_QSTR_wrapx,                          MP_ARG_INT, { .u_int = FT8_BORDER } },
        { MP_QSTR_wrapy,                          MP_ARG_INT, { .u_int = FT8_BORDER } },
        { MP_QSTR_filter,                         MP_ARG_INT, { .u_int = FT8_NEAREST } },
        { MP_QSTR_scalex,                         MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_scaley,                         MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    uint32_t addr = args[ARG_addr].u_int;
    int format = args[ARG_format].u_int;
    if ((format < 0) || (format > 17) || (format == 8) || (format == 12) || (format == 13)) {
        mp_raise_ValueError("Unsupported format");
    }
    uint16_t width = args[ARG_width].u_int;
    uint16_t height = args[ARG_height].u_int;
    uint16_t x = args[ARG_x].u_int;
    uint16_t y = args[ARG_y].u_int;
    uint8_t wrapx = args[ARG_wrapx].u_int & 1;
    uint8_t wrapy = args[ARG_wrapy].u_int & 1;
    uint8_t filter = args[ARG_filter].u_int & 1;
    mp_float_t fx=1.0, fy=1.0;
    int32_t sx=0, sy=0;
    uint16_t stride = (width*2) >> stride_factor[format];
    uint16_t sheight = height;

    if (args[ARG_scalex].u_obj != mp_const_none) {
        fx = mp_obj_get_float(args[ARG_scalex].u_obj);
        fy = fx;
        if (args[ARG_scaley].u_obj != mp_const_none) {
            fy = mp_obj_get_float(args[ARG_scaley].u_obj);
        }
        sx = (int32_t)round((fx * 65536));
        sy = (int32_t)round((fy * 65536));
        width = (int16_t)round((fx * width));
        sheight = (int16_t)round((fy * height));
    }

    FT8_cmd_dl(BITMAP_SOURCE(addr));
    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_LAYOUT_H(stride>>10, height>>9));
    #endif
    FT8_cmd_dl(BITMAP_LAYOUT(format, stride , height));
    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_SIZE_H(width>>9, sheight>>9));
    #endif
    FT8_cmd_dl(BITMAP_SIZE(filter, wrapx, wrapy, width, sheight));

    FT8_cmd_dl(DL_BEGIN | FT8_BITMAPS);
    if (sx != 0) {
        FT8_cmd_dl(CMD_LOADIDENTITY);
        FT8_cmd_scale(sx, sy);
        FT8_cmd_setmatrix();
    }
    FT8_cmd_dl(VERTEX2F(x*16, y*16));
    FT8_cmd_dl(DL_END);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_setbitmap_obj, 0, EVE_setbitmap);

// -------------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_showimage(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_x, ARG_y, ARG_img, ARG_wrapx, ARG_wrapy, ARG_filter, ARG_scalex, ARG_scaley };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_image,        MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_wrapx,                          MP_ARG_INT, { .u_int = FT8_BORDER } },
        { MP_QSTR_wrapy,                          MP_ARG_INT, { .u_int = FT8_BORDER } },
        { MP_QSTR_filter,                         MP_ARG_INT, { .u_int = FT8_NEAREST } },
        { MP_QSTR_scalex,                         MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_scaley,                         MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    display_eve_obj_t *self = pos_args[0];
    _check_inlist(self, 1);

    if (mp_obj_get_type(args[ARG_img].u_obj) != &image_eve_type) {
        mp_raise_ValueError("Image object expected");
    }

    image_eve_obj_t *image = (image_eve_obj_t *)(args[ARG_img].u_obj);
    if (image->loaded == 0) {
        mp_raise_ValueError("Image unloaded");
    }

    uint16_t width = image->width;
    uint16_t height = image->height;
    uint16_t x = args[ARG_x].u_int;
    uint16_t y = args[ARG_y].u_int;
    uint8_t wrapx = args[ARG_wrapx].u_int & 1;
    uint8_t wrapy = args[ARG_wrapy].u_int & 1;
    uint8_t filter = args[ARG_filter].u_int & 1;
    mp_float_t fx=1.0, fy=1.0;
    int32_t sx=0, sy=0;
    uint16_t stride = (width*2) >> stride_factor[image->format];
    uint16_t sheight = height;

    if (args[ARG_scalex].u_obj != mp_const_none) {
        fx = mp_obj_get_float(args[ARG_scalex].u_obj);
        fy = fx;
        if (args[ARG_scaley].u_obj != mp_const_none) {
            fy = mp_obj_get_float(args[ARG_scaley].u_obj);
        }
        sx = (int32_t)round((fx * 65536));
        sy = (int32_t)round((fy * 65536));
        width = (int16_t)round((fx * width));
        sheight = (int16_t)round((fy * height));
    }

    FT8_cmd_dl(BITMAP_SOURCE(image->addr));
    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_LAYOUT_H(stride>>10, height>>9));
    #endif
    FT8_cmd_dl(BITMAP_LAYOUT(image->format, stride , height));
    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_SIZE_H(width>>9, sheight>>9));
    #endif
    FT8_cmd_dl(BITMAP_SIZE(filter, wrapx, wrapy, width, sheight));

    FT8_cmd_dl(DL_BEGIN | FT8_BITMAPS);
    if (sx != 0) {
        FT8_cmd_dl(CMD_LOADIDENTITY);
        FT8_cmd_scale(sx, sy);
        FT8_cmd_setmatrix();
    }
    FT8_cmd_dl(VERTEX2F(x*16, y*16));
    FT8_cmd_dl(DL_END);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_showimage_obj, 0, EVE_showimage);

#ifdef CONFIG_EVE_CHIP_TYPE1
//------------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_playvideo(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_file, ARG_opt, ARG_mode, ARG_vol };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_file,    MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_opt,                       MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_mode,                      MP_ARG_INT,  { .u_int = 1 } },
        { MP_QSTR_vol,                       MP_ARG_INT,  { .u_int = -1 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    //display_eve_obj_t *self = pos_args[0];

    _check_inlist(eve_obj, 0);

    uint32_t addr = ft8_ramg_ptr;
    int size=0, frames=0;
    char *fname = NULL;
    char fullname[128] = {'\0'};
    struct stat sb;
    fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
    uint32_t allowed_opts = FT8_OPT_MONO | FT8_OPT_MEDIAFIFO | FT8_OPT_NOTEAR | FT8_OPT_SOUND | FT8_OPT_FULLSCREEN;
    uint32_t opt = args[ARG_opt].u_int & allowed_opts;
    opt |= FT8_OPT_MEDIAFIFO;
    uint32_t mode = args[ARG_mode].u_int;

    int res = physicalPath(fname, fullname);
    if ((res != 0) || (strlen(fullname) == 0)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
    }

    if (stat(fullname, &sb) != 0) {
        mp_raise_ValueError("Error getting file info");
    }
    int file_len = sb.st_size;

    // try to determine image type
    FILE *fhndl = fopen(fullname, "rb");
    if (fhndl == NULL) {
        mp_raise_ValueError("Error opening file");
    }

    int width=0, height=0, time=0;
    uint8_t buf[256];
    if (fread(buf, 1, 256, fhndl) == 256) {
        buf[255] = 0;
        res = _check_avi(buf, &width, &height, &time, &frames);
        if (res != 0) {
            fclose(fhndl);
            mp_raise_ValueError("Wriong file format, only MJPEG AVI files are supported");
        }
        size = width*height * 2;
        if (size > (FT8_RAM_G_SIZE-addr-65536-4)) {
            fclose(fhndl);
            mp_raise_ValueError("No space left in Eve RAM");
        }
    }
    else {
        fclose(fhndl);
        mp_raise_ValueError("Error reading file info");
    }
    fseek(fhndl, 0, SEEK_SET);

    if (args[ARG_vol].u_int > 0) FT8_memWrite8(REG_VOL_PB, args[ARG_vol].u_int & 0xFF); // set audio volume

    res = FT8_Fifo_init(fhndl);
    if (res != 0) {
        fclose(fhndl);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error initializing mediafifo"));
    }

    res = FT8_sendDataViaMediafifo(fhndl, addr, opt, (uint8_t)mode);
    if (res < 0) return mp_const_false;

    if (mode == 2) {
        mp_obj_t tuple[5];
        tuple[0] = mp_obj_new_int(width);
        tuple[1] = mp_obj_new_int(height);
        tuple[2] = mp_obj_new_int(time);
        tuple[3] = mp_obj_new_int(addr);
        tuple[4] = mp_obj_new_int(frames);

        return mp_obj_new_tuple(5, tuple);
    }

    FT8_Fifo_deinit();
    ESP_LOGD(TAG, "PlayVideo: file length=%d, played=%d\n", file_len, file_len - ft8_stFifo.file_remain);

    fclose(fhndl);

    return mp_const_true;

}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_playvideo_obj, 0, EVE_playvideo);

// ---------------------------------------------------------------
STATIC mp_obj_t EVE_videoframe(mp_obj_t self_in, mp_obj_t addr_in)
{
    //display_eve_obj_t *self = self_in;
    _check_inlist(eve_obj, 1);

    uint32_t addr = mp_obj_get_int(addr_in);
    FT8_cmd_videoframe(addr);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_videoframe_obj, EVE_videoframe);

// ---------------------------------------------
STATIC mp_obj_t EVE_lastframe(mp_obj_t self_in)
{
    //display_eve_obj_t *self = self_in;
    _check_inlist(eve_obj, 0);

    if (FT8_memRead32(ft8_stFifo.fifo_buff-4)) return mp_const_false;
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_lastframe_obj, EVE_lastframe);

// ----------------------------------------------
STATIC mp_obj_t EVE_videobuffer(mp_obj_t self_in)
{
    //display_eve_obj_t *self = self_in;
    _check_inlist(eve_obj, 0);

    int res = FT8_Fifo_service();
    if (res < 0) {
        ESP_LOGE(TAG, "Error in Fifo service");
        FT8_CP_reset();
        return mp_obj_new_int(res);
    }

    return mp_obj_new_int(ft8_stFifo.file_remain);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_videobuffer_obj, EVE_videobuffer);

// ---------------------------------------------
STATIC mp_obj_t EVE_closevideo(mp_obj_t self_in)
{
    //display_eve_obj_t *self = self_in;
    _check_inlist(eve_obj, 0);

    if (ft8_stFifo.pFile) fclose(ft8_stFifo.pFile);
    FT8_Fifo_deinit();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_closevideo_obj, EVE_closevideo);
#endif

// ----------------------------------------------------------
STATIC mp_obj_t EVE_vol_pb(mp_obj_t self_in, mp_obj_t vol_in)
{
    _check_inlist(eve_obj, 0);

    uint8_t volume = mp_obj_get_int(vol_in) & 0xFF;
    FT8_memWrite8(REG_VOL_PB, volume); // set audio volume

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_vol_pb_obj, EVE_vol_pb);

// -------------------------------------------------------------
STATIC mp_obj_t EVE_vol_sound(mp_obj_t self_in, mp_obj_t vol_in)
{
    _check_inlist(eve_obj, 0);

    uint8_t volume = mp_obj_get_int(vol_in) & 0xFF;
    FT8_memWrite8(REG_VOL_SOUND, volume); // set sound volume

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_vol_sound_obj, EVE_vol_sound);

// -------------------------------------------------------------------------------------
STATIC mp_obj_t EVE_sound(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_effect, ARG_note, ARG_vol, ARG_wait };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_effect,    MP_ARG_REQUIRED | MP_ARG_INT,  { .u_int = 255 } },
        { MP_QSTR_note,                        MP_ARG_INT,  { .u_int = 84 } },
        { MP_QSTR_vol,       MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_wait,      MP_ARG_KW_ONLY  | MP_ARG_INT,  { .u_int = -1 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _check_inlist(eve_obj, 0);


    int effect = args[ARG_effect].u_int;
    int note = args[ARG_note].u_int;
    if ((note < 21) || (note > 108)) {
        mp_raise_ValueError("Invalid MIDI note");
    }

    int effect_type = -1;
    if (effect == 0) effect_type = 2; // silence
    else if (effect < 0x09) effect_type = 3; // waves and effects
    else if ((effect >= 0x10) && (effect <= 0x1F)) effect_type = 1; // pips
    else if ((effect == 0x23) || (effect == 0x2C) || ((effect >= 0x30) && (effect <= 0x39))) effect_type = 2; // DTMF tones
    else if ((effect >= 0x40) && (effect <= 0x49)) effect_type = 1; // instruments
    else if ((effect >= 0x50) && (effect <= 0x58)) effect_type = 0; // sound effects
    else if ((effect == 0x60) || (effect == 0x61)) effect_type = 0; // mute/unmute
    if (effect_type < 0) {
        mp_raise_ValueError("Invalid sound effect");
    }

    uint16_t sound = effect;
    if (effect_type & 1) sound |= (note << 8); // has pitch/note

    // stop the previous sound
    //FT8_memWrite16(REG_SOUND, 0);
    //FT8_memWrite8(REG_PLAY, 1);

    if (args[ARG_vol].u_int >= 0) {
        // set the sound volume
        FT8_memWrite8(REG_VOL_SOUND, args[ARG_vol].u_int & 0xFF);
    }

    // start the sound effect
    FT8_memWrite16(REG_SOUND, sound);
    FT8_memWrite8(REG_PLAY, 1);

    if (args[ARG_wait].u_int > 0) {
        if (effect_type & 2) {
            // coutinuous sound, wait for given time
            mp_hal_delay_ms(args[ARG_wait].u_int);
            FT8_memWrite16(REG_SOUND, 0x60); // mute
            FT8_memWrite8(REG_PLAY, 1);
        }
        else {
            // non continuous sound, wait untill finished
            while (FT8_memRead8(REG_PLAY)) {
                mp_hal_delay_ms(3);
            }
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(EVE_sound_obj, 0, EVE_sound);

// ----------------------------------------------------------------------------
// ---- List dump methods -----------------------------------------------------
// ----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
static uint16_t process_eve_command(uint32_t cmd, uint32_t base, uint16_t *idx)
{
    uint16_t proc_bytes = 4;
    uint32_t ccmd = cmd & 0xFF000000;
    *idx += 4;
    *idx &= 0xfff;
    if (cmd == DL_DISPLAY) mp_printf(&mp_plat_print, "DISPLAY (End Display List)\n");
    else if (ccmd == DL_CLEAR_RGB) mp_printf(&mp_plat_print, "CLEAR_COLOR_RGB (0x%06x)\n", cmd & 0xFFFFFF);
    else if (ccmd == DL_CLEAR) mp_printf(&mp_plat_print, "CLEAR (%d, %d, %d)\n", (cmd>>2)&1, (cmd>>1)&1, cmd&1);
    else if (ccmd == DL_CLEAR) mp_printf(&mp_plat_print, "CLEAR (%d, %d, %d)\n", (cmd>>2)&1, (cmd>>1)&1, cmd&1);
    else if (ccmd == 0x0F000000) mp_printf(&mp_plat_print, "CLEAR_COLOR_A (%d)\n", cmd & 0xFF);
    else if (ccmd == 0x11000000) mp_printf(&mp_plat_print, "CLEAR_STENCIL (%d)\n", cmd & 0xFF);
    else if (ccmd == 0x12000000) mp_printf(&mp_plat_print, "CLEAR_TAG (%d)\n", cmd & 0xFF);
    else if (ccmd == 0x03000000) mp_printf(&mp_plat_print, "TAG (%d)\n", cmd & 0xFF);
    else if (ccmd == 0x01400000) mp_printf(&mp_plat_print, "TAG_MASK (%d)\n", cmd & 1);
    else if (ccmd == DL_POINT_SIZE) mp_printf(&mp_plat_print, "POINT_SIZE (%d)\n", cmd & 8192);
    else if (ccmd == 0x0E000000) mp_printf(&mp_plat_print, "LINE_WIDTH (%d)\n", cmd & 4095);
    else if (ccmd == DL_END) mp_printf(&mp_plat_print, "END\n");
    else if (ccmd == DL_BEGIN) {
        if (((cmd & 0x0F)-1) < 9) mp_printf(&mp_plat_print, "BEGIN %s\n", ft8_prim[(cmd & 0x0F)-1]);
        else mp_printf(&mp_plat_print, "BEGIN ? (%d)\n", cmd & 0x0F);
    }
    else if (ccmd == 0x01000000) mp_printf(&mp_plat_print, "BITMAP_SOURCE (%d)\n", cmd & 0xFFFFFF);
    else if (ccmd == 0x07000000) mp_printf(&mp_plat_print, "BITMAP_LAYOUT (%d, %d, %d)\n", (cmd>>19)&31, (cmd>>9)&1023, cmd&511);
    else if (ccmd == 0x28000000) mp_printf(&mp_plat_print, "BITMAP_LAYOUT_H (%d, %d)\n", (cmd>>2)&3, cmd&3);
    else if (ccmd == 0x29000000) mp_printf(&mp_plat_print, "BITMAP_SIZE_H (%d, %d)\n", (cmd>>2)&3, cmd&3);
    else if (ccmd == 0x08000000) mp_printf(&mp_plat_print, "BITMAP_SIZE (%d, %d, %d, %d, %d)\n", (cmd>>20)&1, (cmd>>19)&1, (cmd>>18)&1, (cmd>>9)&512, cmd&511);
    else if (ccmd == 0x05000000) mp_printf(&mp_plat_print, "BITMAP_HANDLE (%0d)\n", cmd & 31);
    else if (ccmd == 0x2A000000) mp_printf(&mp_plat_print, "PALETTE_SOURCE (%d)\n", cmd & 0x3FFFFF);
    else if (ccmd == 0x2B000000) mp_printf(&mp_plat_print, "VERTEX_TRANSLATE_X (%d)\n", cmd & 131071);
    else if (ccmd == 0x2C000000) mp_printf(&mp_plat_print, "VERTEX_TRANSLATE_Y (%d)\n", cmd & 131071);
    else if (ccmd == 0x0b000000) mp_printf(&mp_plat_print, "BLEND_FUNC (%d, %d)\n", (cmd>>3)&7, cmd&7);
    else if (ccmd == 0x09000000) mp_printf(&mp_plat_print, "ALPHA_FUNC (%d, %d)\n", (cmd>>8)&7, cmd&255);
    else if (ccmd == 0x10000000) mp_printf(&mp_plat_print, "COLOR_A (%d)\n", cmd&255);
    else if (ccmd == 0x20000000) mp_printf(&mp_plat_print, "COLOR_MASK (%d, %d, %d, %d)\n", (cmd>>3)&1, (cmd>>2)&1, (cmd>>1)&1, cmd&1);
    else if (ccmd == 0x04000000) mp_printf(&mp_plat_print, "COLOR_RGB (%d, %d, %d)\n", (cmd>>16)&255, (cmd>>8)&255, cmd&255);
    else if (ccmd == 0x22000000) mp_printf(&mp_plat_print, "SAVE_CONTEXT\n");
    else if (ccmd == 0x23000000) mp_printf(&mp_plat_print, "RESTORE_CONTEXT\n");
    else if ((ccmd >= 0x15000000) && (ccmd <= 0x1A000000)) mp_printf(&mp_plat_print, "BITMAP_TRANSFORM_%c (%d)\n", 65+((ccmd>>24)-0x15), cmd&131071);
    #ifdef CONFIG_EVE_CHIP_TYPE1
    else if (ccmd == NOP()) mp_printf(&mp_plat_print, "NOP\n");
    #endif
    else if ((cmd & 0xC0000000) == 0x40000000) mp_printf(&mp_plat_print, "VERTEX2F (%d, %d)\n", (cmd >> 15)&32767, cmd&32767);
    else if ((cmd & 0xC0000000) == 0x80000000) mp_printf(&mp_plat_print, "VERTEXII (%d, %d, %d, %d)\n", (cmd >> 21)&0x1FF, (cmd >> 12)&0x1FF, (cmd >> 7)&31, cmd&127);
    else {
        uint16_t j = *idx;
        switch (cmd) {
            case (CMD_DLSTART):
                mp_printf(&mp_plat_print, "CMD_DLSTART\n");
                break;
            case (CMD_SWAP):
                mp_printf(&mp_plat_print, "CMD_SWAP\n");
                break;
            case (CMD_LOADIDENTITY):
                mp_printf(&mp_plat_print, "CMD_LOADIDENTITY\n");
                break;
            case (CMD_SETMATRIX):
                mp_printf(&mp_plat_print, "CMD_SETMATRIX\n");
                break;
            case (CMD_SCALE): {
                    mp_printf(&mp_plat_print, "CMD_SCALE\n");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(%1.3f,", (float)(cmd / 65536.0));
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "%1.3f)\n", (float)(cmd / 65536.0));
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_COLDSTART):
                mp_printf(&mp_plat_print, "CMD_COLDSTART\n");
                break;
            case (CMD_FGCOLOR): {
                    mp_printf(&mp_plat_print, "CMD_FGCOLOR ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(0x%06x)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_BGCOLOR): {
                    mp_printf(&mp_plat_print, "CMD_BGCOLOR ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(0x%06x)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_INFLATE): {
                    mp_printf(&mp_plat_print, "CMD_INFLATE ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_GETPTR): {
                    mp_printf(&mp_plat_print, "CMD_GETPTR ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_LOADIMAGE): {
                    mp_printf(&mp_plat_print, "CMD_LOADIMAGE ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(%d,", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            #ifdef CONFIG_EVE_CHIP_TYPE1
            case (CMD_MEDIAFIFO): {
                    mp_printf(&mp_plat_print, "CMD_MEDIAFIFO ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(%d,", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_SETBITMAP): {
                    mp_printf(&mp_plat_print, "CMD_SETBITMAP ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(addr=%d, ", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "fmt=%d, w=%d, ", cmd & 0xffff, cmd >> 16);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "h=%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_SETFONT2): {
                    mp_printf(&mp_plat_print, "CMD_SETFONT2: ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(font=%d, ", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "addr=%d, ", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "1st char=%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            #endif
            case (CMD_GETPROPS): {
                    mp_printf(&mp_plat_print, "CMD_GETPROPS ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(addr=%d, ", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "w=%d, ", cmd & 0xfff);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "h=%d)\n", cmd & 0xfff);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_APPEND): {
                    mp_printf(&mp_plat_print, "CMD_APPEND ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(%d,", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            case (CMD_TEXT): {
                    mp_printf(&mp_plat_print, "CMD_TEXT ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(%d,%d), ", cmd & 0xFFFF, cmd >> 16);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "f=%d, opt=%d\n", cmd & 0xFFFF, cmd >> 16);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    mp_printf(&mp_plat_print, "      '");
                    while (1) {
                        char *txt = (char *)&cmd;
                        char ch = *txt;
                        for (int k=0; k<4; k++) {
                            ch = *txt++;
                            if (ch == '\0') break;
                            if ((ch > 0) && (ch < 32)) ch = '.';
                            if (ch > 0) {
                                if (ch >= 32) mp_printf(&mp_plat_print, "%c", ch);
                                else mp_printf(&mp_plat_print, "\\x%2x", ch);
                            }
                        }
                        if (ch == '\0') break;
                        cmd = FT8_memRead32(base + j);
                        j += 4;
                        j &= 0xfff;
                        proc_bytes += 4;
                    }
                    mp_printf(&mp_plat_print, "'\n");
                }
                break;
            case (CMD_SETFONT): {
                    mp_printf(&mp_plat_print, "CMD_SETFONT: ");
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "(font=%d, ", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                    cmd = FT8_memRead32(base + j);
                    mp_printf(&mp_plat_print, "addr=%d)\n", cmd);
                    j += 4;
                    j &= 0xfff;
                    proc_bytes += 4;
                }
                break;
            default:
                mp_printf(&mp_plat_print, "[%08x]\n", cmd);
        }
        *idx = j;
    }
    return proc_bytes;
}

// -------------------------------------------------------------
STATIC mp_obj_t EVE_dumpcmd(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 0);

    bool verbose = true;
    if (n_args > 1) verbose = mp_obj_is_true(args[1]);
    int list_size;
    if (eve_cmdOffset < eve_cmd_start_addr) list_size = FT8_CMDFIFO_SIZE-eve_cmd_start_addr + eve_cmdOffset;
    else list_size = eve_cmdOffset-eve_cmd_start_addr;
    uint32_t cmd;
    uint16_t idx = eve_cmd_start_addr;

    mp_printf(&mp_plat_print, "Command buffer content, size = %d\n", list_size);
    while (list_size > 0) {
        cmd = FT8_memRead32(FT8_RAM_CMD + idx);
        if (verbose) {
            mp_printf(&mp_plat_print, "%04d: ", idx);
            list_size -= process_eve_command(cmd, FT8_RAM_CMD, &idx);
        }
        else {
            mp_printf(&mp_plat_print, "%04d: %08x\n", idx, cmd);
            idx += 4;
            list_size -= 4;
            if (cmd == DL_DISPLAY) break;
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_dumpcmd_obj, 1, 2, EVE_dumpcmd);

// --------------------------------------------------------------
STATIC mp_obj_t EVE_dumplist(size_t n_args, const mp_obj_t *args)
{
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 0);

    bool verbose = true;
    if (n_args > 1) verbose = mp_obj_is_true(args[1]);
    uint16_t list_size = FT8_memRead32(REG_CMD_DL) & 0x1FFF;
    uint32_t cmd;
    uint16_t idx = 0;
    mp_printf(&mp_plat_print, "Display list content, size = %d\n", list_size);
    while (idx < list_size) {
        cmd = FT8_memRead32(FT8_RAM_DL + idx);
        if (verbose) {
            mp_printf(&mp_plat_print, "%04d: ", idx);
            process_eve_command(cmd, FT8_RAM_DL, &idx);
        }
        else {
            mp_printf(&mp_plat_print, "%04d: %08x\n", idx, cmd);
            idx += 4;
        }
        if (cmd == DL_DISPLAY) break;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_dumplist_obj, 1, 2, EVE_dumplist);

// ------------------------------------------------
STATIC mp_obj_t EVE_freeobjects(mp_obj_t self_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    memset(user_fonts, 0, sizeof(user_fonts));
    memset(ramg_objects.objects, 0, sizeof(void*) * ramg_objects.size);
    ramg_objects.count = 0;

    ft8_ramg_ptr = FT8_RAM_G;
    loaded_images = 0;
    loaded_fonts = 0;
    loaded_lists = 0;

    FT8_cmd_memzero(FT8_RAM_G, FT8_RAM_G_SIZE);
    FT8_cmd_execute(250);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_freeobjects_obj, EVE_freeobjects);

// ---------------------------------------------------------------------------------
STATIC mp_obj_t EVE_memWrite(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t buff_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    int addr = mp_obj_get_int(addr_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buff_in, &bufinfo, MP_BUFFER_READ);

    FT8_memWrite_flash_buffer(addr, bufinfo.buf, bufinfo.len, false);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(EVE_memWrite_obj, EVE_memWrite);

// -------------------------------------------------------------------------------
STATIC mp_obj_t EVE_memRead(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t len_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    int addr = mp_obj_get_int(addr_in);
    int len = mp_obj_get_int(len_in);
    vstr_t vstr;
    vstr_init_len(&vstr, len);

    FT8_memRead_buffer(addr, (uint8_t *)vstr.buf, len);

    // Return read data as string
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(EVE_memRead_obj, EVE_memRead);

// -------------------------------------------------------------------------------
STATIC mp_obj_t EVE_memZero(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t len_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    int addr = mp_obj_get_int(addr_in);
    int len = mp_obj_get_int(len_in);

    FT8_cmd_memzero(addr, len);
    FT8_cmd_execute(250);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(EVE_memZero_obj, EVE_memZero);

// --------------------------------------------------------------
STATIC mp_obj_t EVE_memSet(size_t n_args, const mp_obj_t *args) {
    display_eve_obj_t *self = args[0];
    _check_inlist(self, 0);

    int addr = mp_obj_get_int(args[1]);
    int len = mp_obj_get_int(args[2]);
    uint8_t val = mp_obj_get_int(args[3]) & 0xff;

    FT8_cmd_memset(addr, val, len);
    FT8_cmd_execute(250);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(EVE_memSet_obj, 4, 4, EVE_memSet);

// ---------------------------------------------------------
STATIC mp_obj_t EVE_tag(mp_obj_t self_in, mp_obj_t tag_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);

    uint8_t tag = mp_obj_get_int(tag_in) & 0xff;
    if (tag) FT8_cmd_dl(TAG(tag));
    else FT8_cmd_dl(TAG_MASK(0));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_tag_obj, EVE_tag);

// --------------------------------------------------------------
STATIC mp_obj_t EVE_tagmask(mp_obj_t self_in, mp_obj_t mask_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 1);

    bool mask = mp_obj_is_true(mask_in);

    FT8_cmd_dl(TAG_MASK(mask));

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(EVE_tagmask_obj, EVE_tagmask);

// -------------------------------------------
STATIC mp_obj_t EVE_gettag(mp_obj_t self_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    int tag = FT8_get_touch_tag();

    return mp_obj_new_int(tag);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_gettag_obj, EVE_gettag);

// ---------------------------------------------
STATIC mp_obj_t EVE_gettagXY(mp_obj_t self_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    uint32_t tagxy = FT8_memRead32(REG_TOUCH_TAG_XY);
    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(tagxy & 0xFFFF);
    tuple[1] = mp_obj_new_int(tagxy >> 16);

    return mp_obj_new_tuple(2, tuple);

}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_gettagXY_obj, EVE_gettagXY);

// --------------------------------------------
STATIC mp_obj_t EVE_touchXY(mp_obj_t self_in) {
    display_eve_obj_t *self = self_in;
    _check_inlist(self, 0);

    uint32_t touchxy = FT8_memRead32(REG_TOUCH_SCREEN_XY);
    if ((touchxy & 0x80008000)) return mp_const_false;

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(touchxy >> 16);     // X
    tuple[1] = mp_obj_new_int(touchxy & 0xFFFF);  // Y

    return mp_obj_new_tuple(2, tuple);

}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_touchXY_obj, EVE_touchXY);

// -----------------------------------------------
STATIC mp_obj_t EVE_screensize(mp_obj_t self_in) {
    display_eve_obj_t *self = self_in;

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(self->width);
    tuple[1] = mp_obj_new_int(self->height);

    return mp_obj_new_tuple(2, tuple);

}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(EVE_screensize_obj, EVE_screensize);



// ---------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------

// ==== FONT object ============================================

// constructor
//----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t font_eve_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_file };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_file,    MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _check_inlist(eve_obj, 0);
    uint32_t addr = ft8_ramg_ptr; // object address in Eve RAM_G

    char *fname = NULL;
    char fullname[128] = {'\0'};
    int len;
    struct stat sb;
    uint8_t fhandle = MAX_USER_FONTS;

    fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);

    int res = physicalPath(fname, fullname);
    if ((res != 0) || (strlen(fullname) == 0)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
    }

    if (stat(fullname, &sb) != 0) {
        mp_raise_ValueError("Error getting file info");
    }
    len = sb.st_size;
    if (!check_ramg(len)) {
        mp_raise_ValueError("No space left in Eve RAM");
    }
    // Find free font structure
    for (int i=0; i < MAX_USER_FONTS; i++) {
        if (user_fonts[i] == NULL) {
            fhandle = i;
            break;
        }
    }
    if (fhandle >= MAX_USER_FONTS) {
        mp_raise_ValueError("No free font handles left");
    }

    eve_font_metrics_t metrics;
    uint8_t *pmetrics = (uint8_t *)(&metrics.widths[0]);
    uint32_t size = 0;

    // Read the font into Eve RAM_G
    FILE *fhndl = fopen(fullname, "rb");
    if (fhndl != NULL) {
        // get the font metrics block
        if (fread(pmetrics, 1, EVE_FONT_METRICS_SIZE, fhndl) == EVE_FONT_METRICS_SIZE) {
            uint8_t *buff = malloc(1024);
            if (buff == NULL) {
                fclose(fhndl);
                mp_raise_ValueError("Error allocating read buffer");
            }
            uint32_t fptr = addr + EVE_FONT_METRICS_SIZE;
            size += EVE_FONT_METRICS_SIZE;
            // Set the font's raw data address
            if (eve_chip_id < 0x810) metrics.ptr = addr - (metrics.stride * metrics.height) + EVE_FONT_METRICS_SIZE;
            else metrics.ptr = addr + EVE_FONT_METRICS_SIZE;
            // transfer the font metrics block
            FT8_memWrite_flash_buffer(FT8_RAM_G+addr, pmetrics, EVE_FONT_METRICS_SIZE, true);
            // transfer the raw font data
            do {
                len = fread(buff, 1, 1024, fhndl);
                if (len <= 0) break;
                if (len < 1024) memset(buff+len, 0, 1024-len);
                if ((len % 4) != 0) len += (4 - (len % 4));
                FT8_memWrite_flash_buffer(FT8_RAM_G+fptr, buff, len, true);
                fptr += len;
                size += len;
            } while (len > 0);

            free(buff);
        }
        else {
            fclose(fhndl);
            mp_raise_ValueError("Error reading font file");
        }
        fclose(fhndl);
    }
    else {
        mp_raise_ValueError("Error opening font file");
    }

    uint8_t nchars = 0;
    uint8_t firstc = 0;
    uint32_t fformat = metrics.format;
    if (fformat == 17) fformat = 2;
    else if (fformat == 2) fformat = 4;
    else if (fformat == 3) fformat = 8;
    uint8_t minwidth = 99, maxwidth = 0;
    for (int i=0; i<128; i++) {
        if (metrics.widths[i] > 0) {
            nchars++;
            if (firstc == 0) firstc = i;
            if (metrics.widths[i] < minwidth) minwidth = metrics.widths[i];
            if (metrics.widths[i] > maxwidth) maxwidth = metrics.widths[i];
        }
    }

    // Create FONT instance object
    font_eve_obj_t *self = m_new_obj(font_eve_obj_t);
    memset(self, 0, sizeof(font_eve_obj_t));
    self->base.type = &font_eve_type;

    memcpy((uint8_t *)(&self->metrics.widths[0]), pmetrics, sizeof(eve_font_metrics_t));
    self->addr = addr;
    self->size = size;
    self->handle = fhandle;
    self->firstc = firstc;
    self->maxwidth = maxwidth;
    self->minwidth = minwidth;
    self->nchars = nchars;
    self->format = (uint8_t)fformat;
    self->loaded = 1;

    if (!add_ramg_object((void *)self)) {
        mp_raise_ValueError("Error adding ramg object");
    }
    user_fonts[fhandle] = self;

    ft8_ramg_ptr += size;
    loaded_fonts++;

    return MP_OBJ_FROM_PTR(self);
}

// --------------------------------------------
STATIC mp_obj_t FONT_EVE_free(mp_obj_t self_in)
{
    _check_inlist(eve_obj, 0);
    font_eve_obj_t *self = (font_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_true;

    bool res = true;
    if ((self->addr+self->size) < ft8_ramg_ptr) {
        // move other objects in RAM_G above this one
        FT8_cmd_memcpy(self->addr, self->addr + self->size, ft8_ramg_ptr - (self->addr+self->size));
        res = FT8_cmd_execute(250);
    }
    adjust_ramg_objects((void *)self, self->addr, self->size);
    ft8_ramg_ptr -= self->size;
    user_fonts[self->handle] = NULL;
    loaded_fonts--;
    self->loaded = 0;

    if (res) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(FONT_EVE_free_obj, FONT_EVE_free);

// --------------------------------------------
STATIC mp_obj_t FONT_EVE_info(mp_obj_t self_in)
{
    font_eve_obj_t *self = (font_eve_obj_t *)self_in;

    font_eve_printinfo(&mp_plat_print, self, PRINT_STR);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(FONT_EVE_info_obj, FONT_EVE_info);

// ---------------------------------------------
STATIC mp_obj_t FONT_EVE_props(mp_obj_t self_in)
{
    font_eve_obj_t *self = (font_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_false;

    mp_obj_t tuple[7];
    tuple[0] = mp_obj_new_int(self->handle);
    tuple[1] = mp_obj_new_int(self->metrics.width);
    tuple[2] = mp_obj_new_int(self->metrics.height);
    tuple[3] = mp_obj_new_int(self->minwidth);
    tuple[4] = mp_obj_new_int(self->maxwidth);
    tuple[5] = mp_obj_new_int(self->nchars);
    tuple[6] = mp_obj_new_int(self->firstc);

    return mp_obj_new_tuple(7, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(FONT_EVE_props_obj, FONT_EVE_props);

//-------------------------------------------------------------
STATIC const mp_rom_map_elem_t font_eve_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_info),        MP_ROM_PTR(&FONT_EVE_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_free),        MP_ROM_PTR(&FONT_EVE_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_props),       MP_ROM_PTR(&FONT_EVE_props_obj) },
};
STATIC MP_DEFINE_CONST_DICT(font_eve_locals_dict, font_eve_locals_dict_table);

//-----------------------------------
const mp_obj_type_t font_eve_type = {
    { &mp_type_type },
    .name = MP_QSTR_FONT,
    .print = font_eve_printinfo,
    .make_new = font_eve_make_new,
    .locals_dict = (mp_obj_t)&font_eve_locals_dict,
};

// ^^^^ FONT object end ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


// ==== IMAGE object ===========================================

// constructor
//----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t image_eve_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_file, ARG_opt, ARG_type, ARG_format, ARG_width, ARG_height };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_file,    MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_opt,                       MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_type,                      MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_format,                    MP_ARG_INT, { .u_int = FT8_RGB565 } },
        { MP_QSTR_width,                     MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_height,                    MP_ARG_INT,  { .u_int = 0 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _check_inlist(eve_obj, 0);

    char *fname = NULL;
    char fullname[128] = {'\0'};
    int len=0, size=0;
    fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
    #ifdef CONFIG_EVE_CHIP_TYPE1
    uint32_t allowed_opts = FT8_OPT_MONO | FT8_OPT_FULLSCREEN | FT8_OPT_MEDIAFIFO;
    #else
    uint32_t allowed_opts = FT8_OPT_MONO;
    #endif
    uint32_t opt = args[ARG_opt].u_int & allowed_opts;
    opt |= FT8_OPT_NODL;
    uint32_t addr = ft8_ramg_ptr; // address in Eve RAM_G
    int format = args[ARG_format].u_int;
    int width = args[ARG_width].u_int;
    int height = args[ARG_height].u_int;

    int img_type = args[ARG_type].u_int;
    if (img_type < 0) {
        img_type = IMAGE_TYPE_NONE;
        // try to determine image type
        char upr_fname[128] = {'\0'};
        int fname_len = strlen(upr_fname);
        strcpy(upr_fname, fname);
        for (int i=0; i < strlen(upr_fname); i++) {
          upr_fname[i] = toupper((unsigned char) upr_fname[i]);
        }
        if (strstr(upr_fname, ".JPG") == (upr_fname+fname_len-4)) img_type = IMAGE_TYPE_JPG;
        else if (strstr(upr_fname, ".JPEG") == (upr_fname+fname_len-5)) img_type = IMAGE_TYPE_JPG;
        else if (strstr(upr_fname, ".PNG") == (upr_fname+fname_len-4)) img_type = IMAGE_TYPE_PNG;
        else if (strstr(upr_fname, ".RAW") == (upr_fname+fname_len-4)) img_type = IMAGE_TYPE_RAW;
        else if (strstr(upr_fname, ".BIN") == (upr_fname+fname_len-4)) img_type = IMAGE_TYPE_BIN;
        else {
            mp_raise_ValueError("Unsupported image file extension");
        }
    }
    else if ((img_type != IMAGE_TYPE_JPG) && (img_type != IMAGE_TYPE_PNG) && (img_type != IMAGE_TYPE_RAW) && (img_type != IMAGE_TYPE_BIN)) {
        mp_raise_ValueError("Unsupported image file type");
    }

    int res = physicalPath(fname, fullname);
    if ((res != 0) || (strlen(fullname) == 0)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
    }

    // try to determine image type
    FILE *fhndl = fopen(fullname, "rb");
    if (fhndl != NULL) {
        // get file size
        if (fseek(fhndl,0,SEEK_END) == 0) {
            len = ftell(fhndl);
        }
        if (fseek(fhndl,0,SEEK_SET)) len = 0;
        if (len == 0) {
            fclose(fhndl);
            mp_raise_ValueError("Error getting file size");
        }
        uint8_t buf[256];
        if (fread(buf, 1, 256, fhndl) == 256) {
            if ((img_type == IMAGE_TYPE_JPG) || (img_type == IMAGE_TYPE_PNG)) {
                // Check if it is a JPG or PNG file
                res = _check_jpeg(buf, &width, &height);
                if (res != 0) {
                    #if FT8_ENABLE_PNG_LOADING
                    if (eve_chip_id < 0x810) {
                        fclose(fhndl);
                        mp_raise_ValueError("Only JPEG images are supported");
                    }
                    // Not a jpeg image, check if PNG
                    res = _check_png(buf, &width, &height);
                    if (res < 0) {
                        // Not a PNG image
                        fclose(fhndl);
                        ESP_LOGD(TAG, "Not PNG (%d)", res);
                        mp_raise_ValueError("Only JPEG and PNG images are supported");
                    }
                    opt &= 0xFFFFFFFE;
                    if (res == 0) {
                        format = FT8_L8;          // Greyscale
                        opt |= FT8_OPT_MONO;
                    }
                    else if (res == 2) format = FT8_RGB565; // Truecolor
                    else if (res == 6) format = FT8_ARGB4;  // Truecolor with alpha
                    else {
                        fclose(fhndl);
                        mp_raise_ValueError("PNG color type not supported");
                    }
                    #else
                    fclose(fhndl);
                    mp_raise_ValueError("Only JPEG images are supported");
                    #endif
                }
                size = width*height;
                if ((opt & FT8_OPT_MONO) != 0) format = FT8_L8;
                else size *= 2;
                if (size > (FT8_RAM_G_SIZE-addr)) {
                    fclose(fhndl);
                    mp_raise_ValueError("No space left in Eve RAM");
                }
            }
            else if ((img_type == IMAGE_TYPE_RAW) || (img_type == IMAGE_TYPE_BIN)) {
                // Image parameters must be given
                if ((width <= 0) || (height <= 0)) {
                    mp_raise_ValueError("Wrong image dimensions");
                }
                if ((format < 0) || (format > 7)) {
                    mp_raise_ValueError("Unsupported image format");
                }
                size = width*height;
                if (format == FT8_L1) size /= 8;
                else if (format == FT8_L4) size /= 2;
                else if ((format == FT8_ARGB1555) || (format == FT8_ARGB4) || (format == FT8_RGB565)) size *= 2;
                if ((img_type == IMAGE_TYPE_RAW) && (size != len)) {
                    // We know the image size, check it
                    mp_raise_ValueError("Wrong image size");
                }
            }
        }
        else {
            fclose(fhndl);
            mp_raise_ValueError("Error reading file image file");
        }
        fseek(fhndl, 0, SEEK_SET);
    }
    else {
        mp_raise_ValueError("Error opening file");
    }

    // set RAM_G area to zero
    FT8_cmd_memzero(addr, size);
    FT8_cmd_execute(250);
    // Write some values to check the RAM after image decoding
    FT8_memWrite32(addr+size-4, 0x1234abcd);  // This should be overwritten
    FT8_memWrite32(addr+size, size);          // This must be the preserved

    FT8_CP_reset();

    uint32_t tmp1, tmp2;
    if ((img_type == IMAGE_TYPE_JPG) || (img_type == IMAGE_TYPE_PNG)) {
        // === Decode JPG or PNG image ===
        #ifdef CONFIG_EVE_CHIP_TYPE1
        if ((opt & FT8_OPT_MEDIAFIFO) != 0) {
            res = FT8_Fifo_init(fhndl);
            if (res != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error initializing mediafifo"));
            }
            res = FT8_sendDataViaMediafifo(fhndl, addr, opt, 0);
            fclose(fhndl);
            FT8_Fifo_deinit();
        }
        else
        #endif
        {
            res = FT8_cmd_loadimage(addr, opt, fhndl, len, 1);

            fclose(fhndl);
        }
        if (res < 0) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error loading image file into EVE RAM"));
        }
        // == Check if the image was decoded as expected ==
        // Compare width and height returned by EVE Command processor with expected values
        uint32_t offset = FT8_cmd_getprops(addr);
        FT8_cmd_execute(250);
        tmp1 = FT8_memRead32(FT8_RAM_CMD + offset);
        offset += 4;
        offset &= 0x0fff;
        tmp2 = FT8_memRead32(FT8_RAM_CMD + offset);
        if ((tmp1 != width) || (tmp2 != height)) {
            ESP_LOGW(TAG, "Image dimension check failed (%d,%d) <> (%d,%d)\n", tmp1, tmp2, width, height);
        }
    }
    else if (img_type == IMAGE_TYPE_BIN) {
        // === Deflate compressed image ===
        res = FT8_cmd_loadimage(addr, opt, fhndl, len, 2);
        ESP_LOGD(TAG, "Written bytes: %d (%d)\n", res, len);

        fclose(fhndl);
        if (res < 0) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error loading image file into EVE RAM"));
        }
        // == Check if the image was decoded as expected ==
        uint32_t offset = FT8_cmd_getptr();
        FT8_cmd_execute(250);
        tmp1 = FT8_memRead32(FT8_RAM_CMD + offset);
        if (tmp1 != (ft8_ramg_ptr + size)) {
            ESP_LOGW(TAG, "End RAM ptr check failed (%d <> %d)\n", tmp1, ft8_ramg_ptr + size);
        }
    }
    else if (img_type == IMAGE_TYPE_RAW) {
        // === Send RAW image to RAM_G ===
        res = FT8_cmd_loadimage(addr, opt, fhndl, len, 3);
        ESP_LOGD(TAG, "Written bytes: %d (%d)\n", res, len);

        fclose(fhndl);
        if (res < 0) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error loading image file into EVE RAM"));
        }
    }

    // Compare the values written to RAM before decoding
    tmp1 = FT8_memRead32(FT8_RAM_G+addr+size);
    tmp2 = FT8_memRead32(FT8_RAM_G+addr+size-4);
    if ((tmp1 != size) || (tmp2 == 0x1234abcd)) {
        ESP_LOGW(TAG, "RAM check failed: %s %s\n", (tmp1 != size) ? "Too big" : "", (tmp2 == 0x1234abcd) ? "Too small" : "");
    }

    ft8_ramg_ptr += size;
    loaded_images++;

    // Create IMAGE instance object
    image_eve_obj_t *self = m_new_obj(image_eve_obj_t);
    memset(self, 0, sizeof(image_eve_obj_t));
    self->base.type = &image_eve_type;

    self->addr = addr;
    self->width = width;
    self->height = height;
    self->size = size;
    self->format = format;
    self->orig_fmt = img_type;
    self->loaded = 1;

    if (!add_ramg_object((void *)self)) {
        mp_raise_ValueError("Error adding ramg object");
    }

    return MP_OBJ_FROM_PTR(self);
}

// ---------------------------------------------
STATIC mp_obj_t IMAGE_EVE_free(mp_obj_t self_in)
{
    _check_inlist(eve_obj, 0);
    image_eve_obj_t *self = (image_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_true;

    FT8_cmd_memcpy(self->addr, self->addr + self->size, ft8_ramg_ptr - (self->addr+self->size));
    bool res = FT8_cmd_execute(250);
    ft8_ramg_ptr -= self->size;
    loaded_images--;
    self->loaded = 0;

    adjust_ramg_objects((void *)self, self->addr, self->size);

    if (res) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(IMAGE_EVE_free_obj, IMAGE_EVE_free);

// ---------------------------------------------
STATIC mp_obj_t IMAGE_EVE_info(mp_obj_t self_in)
{
    image_eve_obj_t *self = (image_eve_obj_t *)self_in;

    image_eve_printinfo(&mp_plat_print, self, PRINT_STR);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(IMAGE_EVE_info_obj, IMAGE_EVE_info);

// ---------------------------------------------
STATIC mp_obj_t IMAGE_EVE_props(mp_obj_t self_in)
{
    image_eve_obj_t *self = (image_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_false;

    mp_obj_t tuple[5];
    tuple[0] = mp_obj_new_int(self->addr);
    tuple[1] = mp_obj_new_int(self->size);
    tuple[2] = mp_obj_new_int(self->width);
    tuple[3] = mp_obj_new_int(self->height);
    tuple[4] = mp_obj_new_int(self->format);

    return mp_obj_new_tuple(5, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(IMAGE_EVE_props_obj, IMAGE_EVE_props);

//--------------------------------------------------------------
STATIC const mp_rom_map_elem_t image_eve_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_info),        MP_ROM_PTR(&IMAGE_EVE_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_free),        MP_ROM_PTR(&IMAGE_EVE_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_props),       MP_ROM_PTR(&IMAGE_EVE_props_obj) },
};
STATIC MP_DEFINE_CONST_DICT(image_eve_locals_dict, image_eve_locals_dict_table);

//------------------------------------
const mp_obj_type_t image_eve_type = {
    { &mp_type_type },
    .name = MP_QSTR_IMAGE,
    .print = image_eve_printinfo,
    .make_new = image_eve_make_new,
    .locals_dict = (mp_obj_t)&image_eve_locals_dict,
};

// ^^^^ IMAGE object end ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


// ==== LIST object ============================================

// constructor
//----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t LIST_eve_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    mp_raise_ValueError("Can only be created by 'savelist' method");
    return mp_const_none;
}

// ---------------------------------------------
STATIC mp_obj_t LIST_EVE_free(mp_obj_t self_in)
{
    _check_inlist(eve_obj, 0);
    image_eve_obj_t *self = (image_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_true;

    FT8_cmd_memcpy(self->addr, self->addr + self->size, ft8_ramg_ptr - (self->addr+self->size));
    bool res = FT8_cmd_execute(250);
    ft8_ramg_ptr -= self->size;
    loaded_lists--;
    self->loaded = 0;

    adjust_ramg_objects((void *)self, self->addr, self->size);

    if (res) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(LIST_EVE_free_obj, LIST_EVE_free);

// ---------------------------------------------
STATIC mp_obj_t LIST_EVE_info(mp_obj_t self_in)
{
    list_eve_obj_t *self = (list_eve_obj_t *)self_in;

    list_eve_printinfo(&mp_plat_print, self, PRINT_STR);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(LIST_EVE_info_obj, LIST_EVE_info);

// ---------------------------------------------
STATIC mp_obj_t LIST_EVE_props(mp_obj_t self_in)
{
    list_eve_obj_t *self = (list_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_false;

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(self->addr);
    tuple[1] = mp_obj_new_int(self->size);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(LIST_EVE_props_obj, LIST_EVE_props);

//-------------------------------------------------------------
STATIC const mp_rom_map_elem_t list_eve_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_info),        MP_ROM_PTR(&LIST_EVE_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_free),        MP_ROM_PTR(&LIST_EVE_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_props),       MP_ROM_PTR(&LIST_EVE_props_obj) },
};
STATIC MP_DEFINE_CONST_DICT(list_eve_locals_dict, list_eve_locals_dict_table);

//-----------------------------------
const mp_obj_type_t list_eve_type = {
    { &mp_type_type },
    .name = MP_QSTR_LIST,
    .print = list_eve_printinfo,
    .make_new = LIST_eve_make_new,
    .locals_dict = (mp_obj_t)&list_eve_locals_dict,
};

// ^^^^ IMAGE object end ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


// ==== CONSOLE object ===========================================


//--------------------------------------------------------
static uint8_t console_rgb_to_vga(console_eve_obj_t *self)
{
    uint8_t c8 = 0;
    c8 |= (self->fgcolor & 0x0000C0) ? 0x01 : 0;
    c8 |= (self->fgcolor & 0x00C000) ? 0x02 : 0;
    c8 |= (self->fgcolor & 0xC00000) ? 0x04 : 0;
    c8 |= (self->fgcolor & 0x808080) ? 0x08 : 0;

    c8 |= (self->bgcolor & 0x0000C0) ? 0x10 : 0;
    c8 |= (self->bgcolor & 0x00C000) ? 0x20 : 0;
    c8 |= (self->bgcolor & 0xC00000) ? 0x40 : 0;
    c8 |= (self->bgcolor & 0x808080) ? 0x80 : 0;

    return c8;
}

/*
//--------------------------------------------------------------------
static void console_vga_to_rgb(console_eve_obj_t *self, uint8_t color)
{
    uint8_t c32 = 0;
    uint32_t ci = (color & 0x08) ? 0x80 : 0;
    c32 |= ((color & 0x01) ? 0x7f : 0) | ci;
    c32 |= ((color & 0x02) ? 0x7f00 : 0) | (ci<<8);
    c32 |= ((color & 0x04) ? 0x7f0000 : 0) | (ci<<16);
    self->fgcolor = c32;

    c32 = 0;
    ci = (color & 0x80) ? 0x80 : 0;
    c32 |= ((color & 0x01) ? 0x7f : 0) | ci;
    c32 |= ((color & 0x02) ? 0x7f00 : 0) | (ci<<8);
    c32 |= ((color & 0x04) ? 0x7f0000 : 0) | (ci<<16);
    self->bgcolor = c32;
}
*/

//----------------------------------------------------------------------
static void console_memclear(console_eve_obj_t *self, int addr, int len)
{
    if (self->type == FT8_TEXTVGA) {
        len *= 2;
        uint8_t row[len];
        for (int i=0; i<len; i+=2) {
            row[i] = 0x20;
            row[i+1] = self->vgacolor;
        }
        FT8_cmd_memwrite(addr , row, len);
    }
    else FT8_cmd_memset(addr, 0x20, len);
    FT8_cmd_execute(250);
}

//-------------------------------------------------
static void console_cursor(console_eve_obj_t *self)
{
    if (!self->show_cursor) return;

    uint8_t crs[2];
    crs [0] = 220;
    crs[1] = 0x0E;
    if (self->type == FT8_TEXTVGA) FT8_cmd_memwrite(self->curr_addr, crs, 2);
    else FT8_cmd_memwrite(self->curr_addr, crs, 1);
    FT8_cmd_execute(250);
}

//------------------------------------------------
static void console_clear(console_eve_obj_t *self)
{
    if (self->type == FT8_TEXTVGA) {
        for (int i=0; i<self->height; i++) {
            console_memclear(self, self->addr + (self->rowbytes * i), self->width);
        }
    }
    else {
        FT8_cmd_memset(self->addr, 0x20, self->size);
        FT8_cmd_execute(250);
    }
}

//-------------------------------------------------
static void console_scroll(console_eve_obj_t *self)
{
    // scroll
    FT8_cmd_memcpy(self->addr, self->addr + self->rowbytes, self->size - self->rowbytes);
    FT8_cmd_execute(250);
    // clear the last row
    self->y = self->height - 1;
    self->x = 0;
    self->curr_addr = self->addr + (self->size - self->rowbytes);
    console_memclear(self, self->addr + (self->rowbytes * self->y), self->width);
    console_cursor(self);
    return;
}

//-----------------------------------------------------------------------------------------
static void console_row_add(console_eve_obj_t *self, uint8_t * row, uint8_t c, int *rowpos)
{
    uint8_t n = 1;
    if (self->type == FT8_TEXTVGA) {
        row[*rowpos] = c;
        row[*rowpos+1] = self->vgacolor;
        n = 2;
    }
    else row[*rowpos] = c;
    *rowpos += n;
}

//----------------------------------------------------------------------------
static void _console_write_row(console_eve_obj_t *self, uint8_t *row, int len)
{
    if (len == 0) return;
    FT8_cmd_memwrite(self->curr_addr, row, len);
    FT8_cmd_execute(250);
}

//------------------------------------------------------------
static void _console_addrinc(console_eve_obj_t *self, int len)
{
    if (len == 0) return;
    // set cursor and current address
    self->curr_addr += len;
    self->y = (self->curr_addr-self->addr) / self->rowbytes;
    self->x = (self->curr_addr-self->addr) % self->rowbytes;
    if (self->type == FT8_TEXTVGA) self->x /= 2;
    if (self->y >= self->height) console_scroll(self);
}

//---------------------------------------------------------------------------
static void console_write_row(console_eve_obj_t *self, uint8_t *row, int len)
{
    _console_write_row(self, row, len);
    _console_addrinc(self, len);
}

//---------------------------------------------------------------------------
static void console_write_blank_eol(console_eve_obj_t *self, uint8_t addrinc)
{
    int len = 0;
    if (self->type == FT8_TEXTVGA) {
        len = self->rowbytes - (self->x*2);
        uint8_t buff16[len];
        for (int i=0; i<len; i+=2) {
            buff16[i] = 0x20;
            buff16[i+1] = self->vgacolor;
        }
        _console_write_row(self, buff16, len);
    }
    else {
        len = self->rowbytes - self->x;
        uint8_t buff8[len];
        for (int i=0; i<len; i++) {
            buff8[i] = 0x20;
        }
        _console_write_row(self, buff8, len);
    }
    if (addrinc) _console_addrinc(self, len);
}

//---------------------------------------------------
static void console_new_line(console_eve_obj_t *self)
{
    self->x = 0;
    self->y++;
    if (self->y >= self->height) console_scroll(self);
    else {
        if (self->type == FT8_TEXTVGA) self->curr_addr = self->addr + (self->rowbytes * self->y) + (self->x * 2);
        else self->curr_addr = self->addr + (self->rowbytes * self->y) + self->x;
    }
}


// constructor
//------------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t console_eve_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_x, ARG_y, ARG_type, ARG_scale };
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
            { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
            { MP_QSTR_type,         MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = FT8_TEXT8X8 } },
            { MP_QSTR_scale,        MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _check_inlist(eve_obj, 0);

    int format = args[ARG_type].u_int;
    if ((format != FT8_TEXT8X8) && (format != FT8_TEXTVGA)) {
        mp_raise_ValueError("Unsupported console type");
    }
    uint16_t x = args[ARG_x].u_int;
    uint16_t y = args[ARG_y].u_int;
    uint16_t rowbytes = x;
    uint32_t size = x * y;
    if (format == FT8_TEXTVGA) {
        size *= 2;
        rowbytes *= 2;
    }

    uint32_t addr = ft8_ramg_ptr; // address in Eve RAM_G
    if (size > (FT8_RAM_G_SIZE-addr)) {
        mp_raise_ValueError("No space left in Eve RAM");
    }

    mp_float_t fscale=1.0;
    if (args[ARG_scale].u_obj != mp_const_none) {
        fscale = mp_obj_get_float(args[ARG_scale].u_obj);
        if (fscale < 1.2) fscale = 1.0;
    }

    // Create console instance object
    console_eve_obj_t *self = m_new_obj(console_eve_obj_t);
    memset(self, 0, sizeof(console_eve_obj_t));
    self->base.type = &console_eve_type;

    self->addr = addr;
    self->size = size;
    self->curr_addr = addr;
    self->x = 0;
    self->y = 0;
    self->width = x;
    self->height = y;
    self->rowbytes = rowbytes;
    self->scale = fscale;
    self->rowspace = 0;
    self->fgcolor = 0x0000FF00;
    self->bgcolor = 0;
    self->type = format;
    self->show_cursor = 0;
    self->wrap = 0;
    self->loaded = 1;
    self->vgacolor = console_rgb_to_vga(self);

    if (!add_ramg_object((void *)self)) {
        mp_raise_ValueError("Error adding ramg object");
    }

    ft8_ramg_ptr += size;

    console_clear(self);
    console_cursor(self);

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------
STATIC mp_obj_t CONSOLE_EVE_free(mp_obj_t self_in)
{
    _check_inlist(eve_obj, 0);
    console_eve_obj_t *self = (console_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_true;

    FT8_cmd_memcpy(self->addr, self->addr + self->size, ft8_ramg_ptr - (self->addr+self->size));
    bool res = FT8_cmd_execute(250);
    ft8_ramg_ptr -= self->size;
    self->loaded = 0;

    adjust_ramg_objects((void *)self, self->addr, self->size);

    if (res) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(CONSOLE_EVE_free_obj, CONSOLE_EVE_free);

//--------------------------------------------------------------------
STATIC mp_obj_t CONSOLE_EVE_clear(size_t n_args, const mp_obj_t *args)
{
    _check_inlist(eve_obj, 0);
    console_eve_obj_t *self = (console_eve_obj_t *)args[0];
    if (self->loaded == 0) return mp_const_true;

    if (n_args > 1) {
        int y = mp_obj_get_int(args[1]);
        if (y < 0) y = 0;
        if (y >= self->height) y = self->height - 1;
        console_memclear(self, self->addr + (self->rowbytes * y), self->width);
    }
    else {
        self->x = 0;
        self->y = 0;
        self->fgcolor = 0x0000FF00;
        self->bgcolor = 0;
        self->vgacolor = console_rgb_to_vga(self);
        console_clear(self);
    }
    self->curr_addr = self->addr;
    console_cursor(self);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(CONSOLE_EVE_clear_obj, 1, 2, CONSOLE_EVE_clear);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t CONSOLE_EVE_show(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_x, ARG_y, ARG_rowspace, ARG_filter, ARG_cursor, ARG_color, ARG_bgcolor, ARG_wrap };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_rowspace,     MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_filter,       MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = FT8_NEAREST } },
        { MP_QSTR_cursor,       MP_ARG_KW_ONLY  | MP_ARG_BOOL,{ .u_bool = false } },
        { MP_QSTR_fgcolor,      MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_bgcolor,      MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_wrap,         MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
    };
    _check_inlist(eve_obj, 1);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    console_eve_obj_t *self = pos_args[0];

    if (self->loaded == 0) {
        mp_raise_ValueError("Unloaded");
    }

    if (args[ARG_color].u_int >= 0) {
        self->fgcolor = args[ARG_color].u_int;
        self->vgacolor = console_rgb_to_vga(self);
    }
    if (args[ARG_bgcolor].u_int >= 0) {
        self->bgcolor = args[ARG_bgcolor].u_int;
        self->vgacolor = console_rgb_to_vga(self);
    }
    if (args[ARG_wrap].u_int >= 0) self->wrap = args[ARG_wrap].u_int & 1;
    self->show_cursor = args[ARG_cursor].u_bool;
    int rowspace = self->rowspace;
    if (args[ARG_rowspace].u_int >= 0) {
        rowspace = args[ARG_rowspace].u_int;
        if (rowspace < 0) rowspace = 0;
        if (rowspace > 8) rowspace = 8;
    }
    if (self->rowspace != rowspace) self->rowspace = rowspace;
    int x = args[ARG_x].u_int;
    int y = args[ARG_y].u_int;
    uint16_t width = self->width * 8 ;
    uint16_t swidth = width;
    uint16_t height = 8;
    uint16_t sheight = height;
    uint16_t stride = (width*2) >> stride_factor[self->type];
    uint8_t filter = args[ARG_filter].u_int & 1;
    int32_t scale=0;
    if (self->scale > 1.19) {
        //filter = FT8_BILINEAR;
        swidth = (int16_t)(self->scale * width);
        sheight = (int16_t)(self->scale * height);
        scale = (int32_t)(self->scale * 65536);
    }

    if (self->type == FT8_TEXTVGA) {
        height *= 2;
        sheight *= 2;
        // draw backgroung rectangle
        FT8_cmd_dl(DL_COLOR_RGB | (self->bgcolor & 0x00ffffff));
        FT8_cmd_rect(x, y, x+swidth, y+((sheight+self->rowspace)*self->height), 1);
        // to draw characters non transparent
        FT8_cmd_dl(DL_COLOR_RGB | 0x00ffffff);
        FT8_cmd_dl(BLEND_FUNC(FT8_ONE, FT8_ZERO));
    }
    else {
        // draw backgroung rectangle
        FT8_cmd_dl(DL_COLOR_RGB | (self->bgcolor & 0x00ffffff));
        FT8_cmd_rect(x, y, x+swidth, y+((sheight+self->rowspace)*self->height), 1);
    }

    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_LAYOUT_H(stride>>10, height>>9));
    #endif
    FT8_cmd_dl(BITMAP_LAYOUT(self->type, stride , height));
    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_SIZE_H(swidth>>9, sheight>>9));
    #endif
    FT8_cmd_dl(BITMAP_SIZE(filter, FT8_BORDER, FT8_BORDER, swidth, sheight));
    if (scale) {
        FT8_cmd_dl(CMD_LOADIDENTITY);
        FT8_cmd_scale(scale, scale);
        FT8_cmd_setmatrix();
    }
    FT8_cmd_dl(DL_BEGIN | FT8_BITMAPS);
    if (self->type == FT8_TEXT8X8) FT8_cmd_dl(DL_COLOR_RGB | (self->fgcolor & 0x00ffffff));

    for (int i=0; i<self->height; i++) {
        FT8_cmd_dl(BITMAP_SOURCE(self->addr + (self->rowbytes * i)));
        FT8_cmd_dl(VERTEX2F(x*16, (y + (i*(sheight+self->rowspace)))*16));
    }
    FT8_cmd_dl(DL_END);
    if (self->type == FT8_TEXTVGA) {
        FT8_cmd_dl(BLEND_FUNC(FT8_SRC_ALPHA, FT8_ONE_MINUS_SRC_ALPHA));
    }
    if (scale) {
        FT8_cmd_dl(CMD_LOADIDENTITY);
        FT8_cmd_scale(65536, 65536);
        FT8_cmd_setmatrix();
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(CONSOLE_EVE_show_obj, 0, CONSOLE_EVE_show);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t CONSOLE_EVE_text(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_text, ARG_x, ARG_y, ARG_color, ARG_bgcolor };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_text,         MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_x,                              MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_y,                              MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_fgcolor,                        MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_bgcolor,                        MP_ARG_INT, { .u_int = -1 } },
    };
    _check_inlist(eve_obj, 0);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    console_eve_obj_t *self = pos_args[0];
    if (self->loaded == 0) {
        mp_raise_ValueError("Unloaded");
    }

    const char *st = mp_obj_str_get_str(args[ARG_text].u_obj);
    if (strlen(st) == 0) return mp_const_none;

    // create the row buffer
    uint8_t row[self->rowbytes];

    int idx=0;
    int len = strlen(st);
    int rowstart, rowpos, lastblank;
    uint8_t bpc = (self->type == FT8_TEXTVGA) ? 2 : 1;
    uint16_t x = self->x;
    uint16_t y = self->y;
    if (args[ARG_color].u_int >= 0) {
        self->fgcolor = args[ARG_color].u_int;
        self->vgacolor = console_rgb_to_vga(self);
    }
    if (args[ARG_bgcolor].u_int >= 0) {
        self->bgcolor = args[ARG_bgcolor].u_int;
        self->vgacolor = console_rgb_to_vga(self);
    }

    if ((args[ARG_x].u_int >= 0) || (args[ARG_y].u_int >= 0)) {
        if (args[ARG_x].u_int >= 0) x = args[ARG_x].u_int;
        if (args[ARG_y].u_int >= 0) y = args[ARG_y].u_int;
        if ((x >= self->width) || (y >= self->height)) {
            mp_raise_ValueError("Position outside console area");
        }
        self->x = x;
        self->y = y;
        if (self->type == FT8_TEXTVGA) self->curr_addr = self->addr + (self->rowbytes * y) + (x * 2);
        else self->curr_addr = self->addr + (self->rowbytes * y) + x;
        console_cursor(self);
    }

    rowstart = self->x;
    rowpos = rowstart;
    lastblank = -1;

    uint8_t vgacolor = self->vgacolor; // save original vga color
    while (idx < len) {
        if (st[idx] == 0x08) {
            // *** ('\b'), set color if in TEXTVGA mode
            idx++;
            if (idx >= len) break;
            if (self->type == FT8_TEXTVGA) {
                // set color
                if (st[idx] == 0xff) self->vgacolor = vgacolor;
                else self->vgacolor = st[idx];
            }
            idx++;
        }
        else if (st[idx] == 0x0A) {
            // *** new line ('\n')
            console_write_row(self, row+rowstart, rowpos-rowstart);
            console_new_line(self);
            rowstart = self->x;
            rowpos = rowstart;
            lastblank = -1;
            idx++;
        }
        else if (st[idx] == 0x0D) {
            // *** clear to the end of line ('\r'), preserve position
            console_write_row(self, row+rowstart, rowpos-rowstart);
            if (self->x > 0) console_write_blank_eol(self, 0);
            rowstart = self->x;
            rowpos = rowstart;
            lastblank = -1;
            idx++;
        }
        /*
        else if (st[idx] == '\t') {
            // *** tab, write 4 spaces
            if ((self->rowbytes - rowpos) >= 4) {
                for (int n=0; n<4; n++) {
                    console_row_add(self, row, 0x20, &rowpos);
                }
            }
            idx++;
        }
        */
        else if (st[idx] < 32) idx++; // ignore non-printable characters
        else {
            // printable character, add it to row buffer
            if (st[idx] == 0x20) lastblank = rowpos;
            console_row_add(self, row, st[idx], &rowpos);

            if (rowpos >= self->rowbytes) {
                // === row buffer filled, write it ===
                if ((self->wrap) && (lastblank >= 0)) {
                    // word wrapping is used, write up to the last blank character
                    // and advance row position
                    console_write_row(self, row+rowstart, lastblank-rowstart);
                    if (self->x > 0) console_write_blank_eol(self, 1);
                    // write remaining characters
                    rowstart = lastblank + bpc;
                }
                console_write_row(self, row+rowstart, rowpos-rowstart);
                rowstart = self->x;
                rowpos = rowstart;
                lastblank = -1;
            }
            idx++;
        }
    }
    if (rowpos > 0) console_write_row(self, row+rowstart, rowpos-rowstart);

    self->vgacolor = vgacolor; // restore original vga color
    console_cursor(self);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(CONSOLE_EVE_text_obj, 0, CONSOLE_EVE_text);


//----------------------------------------------------------------
STATIC const mp_rom_map_elem_t console_eve_locals_dict_table[] = {
    // instance methods
    //{ MP_ROM_QSTR(MP_QSTR_info),        MP_ROM_PTR(&CONSOLE_EVE_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_free),        MP_ROM_PTR(&CONSOLE_EVE_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_show),        MP_ROM_PTR(&CONSOLE_EVE_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_text),        MP_ROM_PTR(&CONSOLE_EVE_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),       MP_ROM_PTR(&CONSOLE_EVE_clear_obj) },
};
STATIC MP_DEFINE_CONST_DICT(console_eve_locals_dict, console_eve_locals_dict_table);


//-----------------------------------
const mp_obj_type_t console_eve_type = {
    { &mp_type_type },
    .name = MP_QSTR_CONSOLE,
    .print = console_eve_printinfo,
    .make_new = console_eve_make_new,
    .locals_dict = (mp_obj_t)&console_eve_locals_dict,
};

// ^^^^ CONSOLE object end ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


#ifdef CONFIG_MICROPY_USE_TFT

// ==== TFT object ===============================================


// constructor
//---------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t tft_eve_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_x, ARG_y, ARG_type };
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
            { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
            { MP_QSTR_type,         MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = FT8_RGB332 } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    _check_inlist(eve_obj, 0);

    int format = args[ARG_type].u_int;
    if ((format != FT8_RGB332) && (format != FT8_RGB565)) {
        mp_raise_ValueError("Unsupported console type");
    }
    uint16_t x = args[ARG_x].u_int;
    uint16_t y = args[ARG_y].u_int;
    uint32_t size = x * y;
    uint16_t rowsize = x;
    uint8_t bpp = 1;
    if (format == FT8_RGB565) {
        size *= 2;
        rowsize *= 2;
        bpp = 2;
    }

    uint32_t addr = ft8_ramg_ptr; // address in Eve RAM_G
    if (size > (FT8_RAM_G_SIZE-addr)) {
        mp_raise_ValueError("No space left in Eve RAM");
    }

    // Create tft instance object
    tft_eve_obj_t *self = m_new_obj(tft_eve_obj_t);
    memset(self, 0, sizeof(tft_eve_obj_t));
    self->base.type = &tft_eve_type;

    self->addr = addr;
    self->size = size;
    self->width = x;
    self->height = y;
    self->rowsize = rowsize;
    self->byte_per_pixel = bpp;
    self->type = format;
    self->loaded = 1;
    self->prev_tft_mode = tft_active_mode;

    if (!add_ramg_object((void *)self)) {
        mp_raise_ValueError("Error adding ramg object");
    }

    ft8_ramg_ptr += size;
    eve_tft_obj = NULL;

    FT8_cmd_memzero(self->addr, self->size);
    FT8_cmd_execute(250);

    return MP_OBJ_FROM_PTR(self);
}

//--------------------------------------------
STATIC mp_obj_t TFT_EVE_free(mp_obj_t self_in)
{
    _check_inlist(eve_obj, 0);
    tft_eve_obj_t *self = (tft_eve_obj_t *)self_in;
    if (self->loaded == 0) return mp_const_true;

    FT8_cmd_memcpy(self->addr, self->addr + self->size, ft8_ramg_ptr - (self->addr+self->size));
    bool res = FT8_cmd_execute(250);
    ft8_ramg_ptr -= self->size;
    self->loaded = 0;

    adjust_ramg_objects((void *)self, self->addr, self->size);

    if (res) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(TFT_EVE_free_obj, TFT_EVE_free);

//------------------------------------------------
static void _tft_eve_activate(tft_eve_obj_t *self)
{
    self->prev_tft_mode = tft_active_mode;
    tft_active_mode = TFT_MODE_EVE;
    disp_spi = NULL;
    // === SET TFT GLOBAL VARIABLES
    TFT_saveClipWin();
    _width = self->width;
    _height = self->height;
    dispWin.x1 = 0;
    dispWin.y1 = 0;
    dispWin.x2 = _width;
    dispWin.y2 = _height;
}

//------------------------------------------------
STATIC mp_obj_t TFT_EVE_activate(mp_obj_t self_in)
{
    tft_eve_obj_t *self = (tft_eve_obj_t *)self_in;
    eve_tft_obj = self;
    if (tft_active_mode != TFT_MODE_EVE) _tft_eve_activate(self);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(TFT_EVE_activate_obj, TFT_EVE_activate);

//--------------------------------------------------
STATIC mp_obj_t TFT_EVE_deactivate(mp_obj_t self_in)
{
    tft_eve_obj_t *self = (tft_eve_obj_t *)self_in;
    if (tft_active_mode == TFT_MODE_EVE) {
        tft_active_mode = self->prev_tft_mode;
        eve_tft_obj = NULL;
        TFT_restoreClipWin();
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(TFT_EVE_deactivate_obj, TFT_EVE_deactivate);

//--------------------------------------------------------------------
STATIC mp_obj_t TFT_EVE_clear(size_t n_args, const mp_obj_t *args)
{
    _check_inlist(eve_obj, 0);
    tft_eve_obj_t *self = (tft_eve_obj_t *)args[0];
    if (self->loaded == 0) return mp_const_true;

    FT8_cmd_memzero(self->addr, self->size);
    FT8_cmd_execute(250);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(TFT_EVE_clear_obj, 1, 2, TFT_EVE_clear);

//-----------------------------------------------------------------------------------------
STATIC mp_obj_t TFT_EVE_show(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_x, ARG_y };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_x,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_y,            MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
    };
    _check_inlist(eve_obj, 1);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    tft_eve_obj_t *self = pos_args[0];

    if (self->loaded == 0) {
        mp_raise_ValueError("Unloaded");
    }

    int x = args[ARG_x].u_int;
    int y = args[ARG_y].u_int;

    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_LAYOUT_H(self->rowsize>>10, self->height>>9));
    #endif
    FT8_cmd_dl(BITMAP_LAYOUT(self->type, self->rowsize , self->height));
    #ifdef CONFIG_EVE_CHIP_TYPE1
    FT8_cmd_dl(BITMAP_SIZE_H(self->width>>9, self->height>>9));
    #endif
    FT8_cmd_dl(BITMAP_SIZE(FT8_NEAREST, FT8_BORDER, FT8_BORDER, self->width, self->height));
    FT8_cmd_dl(DL_BEGIN | FT8_BITMAPS);
    FT8_cmd_dl(BITMAP_SOURCE(self->addr));
    FT8_cmd_dl(VERTEX2F(x*16, y*16));
    FT8_cmd_dl(DL_END);

    eve_tft_obj = self;
    if (tft_active_mode != TFT_MODE_EVE) _tft_eve_activate(self);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(TFT_EVE_show_obj, 0, TFT_EVE_show);

//------------------------------------------------------------
STATIC const mp_rom_map_elem_t tft_eve_locals_dict_table[] = {
    // instance methods
    //{ MP_ROM_QSTR(MP_QSTR_info),                MP_ROM_PTR(&TFT_EVE_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_free),                MP_ROM_PTR(&TFT_EVE_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_select),              MP_ROM_PTR(&TFT_EVE_activate_obj) },
    { MP_ROM_QSTR(MP_QSTR_deselect),            MP_ROM_PTR(&TFT_EVE_deactivate_obj) },
    { MP_ROM_QSTR(MP_QSTR_show),                MP_ROM_PTR(&TFT_EVE_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),               MP_ROM_PTR(&TFT_EVE_clear_obj) },

    { MP_ROM_QSTR(MP_QSTR_pixel),               MP_ROM_PTR(&display_tft_drawPixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_line),                MP_ROM_PTR(&display_tft_drawLine_obj) },
    { MP_ROM_QSTR(MP_QSTR_lineByAngle),         MP_ROM_PTR(&display_tft_drawLineByAngle_obj) },
    { MP_ROM_QSTR(MP_QSTR_triangle),            MP_ROM_PTR(&display_tft_drawTriangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle),              MP_ROM_PTR(&display_tft_drawCircle_obj) },
    { MP_ROM_QSTR(MP_QSTR_ellipse),             MP_ROM_PTR(&display_tft_drawEllipse_obj) },
    { MP_ROM_QSTR(MP_QSTR_arc),                 MP_ROM_PTR(&display_tft_drawArc_obj) },
    { MP_ROM_QSTR(MP_QSTR_polygon),             MP_ROM_PTR(&display_tft_drawPoly_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect),                MP_ROM_PTR(&display_tft_drawRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_roundrect),           MP_ROM_PTR(&display_tft_drawRoundRect_obj) },
    { MP_ROM_QSTR(MP_QSTR_font),                MP_ROM_PTR(&display_tft_setFont_obj) },
    { MP_ROM_QSTR(MP_QSTR_fontSize),            MP_ROM_PTR(&display_tft_getFontSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_text),                MP_ROM_PTR(&display_tft_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_screensize),          MP_ROM_PTR(&display_tft_getSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_setwin),              MP_ROM_PTR(&display_tft_setclipwin_obj) },
    { MP_ROM_QSTR(MP_QSTR_resetwin),            MP_ROM_PTR(&display_tft_resetclipwin_obj) },

    // Constants
    { MP_ROM_QSTR(MP_QSTR_CENTER),              MP_ROM_INT(CENTER) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT),               MP_ROM_INT(RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_BOTTOM),              MP_ROM_INT(BOTTOM) },
    { MP_ROM_QSTR(MP_QSTR_LASTX),               MP_ROM_INT(LASTX) },
    { MP_ROM_QSTR(MP_QSTR_LASTY),               MP_ROM_INT(LASTY) },

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
};
STATIC MP_DEFINE_CONST_DICT(tft_eve_locals_dict, tft_eve_locals_dict_table);


//-----------------------------------
const mp_obj_type_t tft_eve_type = {
    { &mp_type_type },
    .name = MP_QSTR_TFT,
    .print = tft_eve_printinfo,
    .make_new = tft_eve_make_new,
    .locals_dict = (mp_obj_t)&tft_eve_locals_dict,
};

// ^^^^ TFT object ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#endif

//================================================================
STATIC const mp_rom_map_elem_t display_eve_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init),                MP_ROM_PTR(&display_eve_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_config),              MP_ROM_PTR(&display_eve_config_obj) },
    //{ MP_ROM_QSTR(MP_QSTR_deinit),              MP_ROM_PTR(&display_tft_deinit_obj) },

    { MP_ROM_QSTR(MP_QSTR_calibrate),           MP_ROM_PTR(&EVE_calibrate_obj) },
    { MP_ROM_QSTR(MP_QSTR_startlist),           MP_ROM_PTR(&EVE_startlist_obj) },
    { MP_ROM_QSTR(MP_QSTR_endlist),             MP_ROM_PTR(&EVE_endlist_obj) },
    { MP_ROM_QSTR(MP_QSTR_savelist),            MP_ROM_PTR(&EVE_savelist_obj) },
    { MP_ROM_QSTR(MP_QSTR_appendlist),          MP_ROM_PTR(&EVE_appendlist_obj) },
    { MP_ROM_QSTR(MP_QSTR_dumplist),            MP_ROM_PTR(&EVE_dumplist_obj) },
    { MP_ROM_QSTR(MP_QSTR_dumpcmd),             MP_ROM_PTR(&EVE_dumpcmd_obj) },
    { MP_ROM_QSTR(MP_QSTR_point),               MP_ROM_PTR(&EVE_point_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle),              MP_ROM_PTR(&EVE_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_line),                MP_ROM_PTR(&EVE_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_strips),              MP_ROM_PTR(&EVE_strips_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect),                MP_ROM_PTR(&EVE_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_rectangle),           MP_ROM_PTR(&EVE_rectangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_triangle),            MP_ROM_PTR(&EVE_triangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_scissorSize),         MP_ROM_PTR(&EVE_scissorSize_obj) },
    { MP_ROM_QSTR(MP_QSTR_scissorXY),           MP_ROM_PTR(&EVE_scissorXY_obj) },
    { MP_ROM_QSTR(MP_QSTR_text),                MP_ROM_PTR(&EVE_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_number),              MP_ROM_PTR(&EVE_number_obj) },
    { MP_ROM_QSTR(MP_QSTR_button),              MP_ROM_PTR(&EVE_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_keys),                MP_ROM_PTR(&EVE_keys_obj) },
    { MP_ROM_QSTR(MP_QSTR_clock),               MP_ROM_PTR(&EVE_clock_obj) },
    { MP_ROM_QSTR(MP_QSTR_gauge),               MP_ROM_PTR(&EVE_gauge_obj) },
    { MP_ROM_QSTR(MP_QSTR_toggle),              MP_ROM_PTR(&EVE_toggle_obj) },
    { MP_ROM_QSTR(MP_QSTR_scrollbar),           MP_ROM_PTR(&EVE_scrollbar_obj) },
    { MP_ROM_QSTR(MP_QSTR_slider),              MP_ROM_PTR(&EVE_slider_obj) },
    { MP_ROM_QSTR(MP_QSTR_progress),            MP_ROM_PTR(&EVE_progress_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),               MP_ROM_PTR(&EVE_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_backlight),           MP_ROM_PTR(&EVE_backlight_obj) },
    { MP_ROM_QSTR(MP_QSTR_color),               MP_ROM_PTR(&EVE_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_fgcolor),             MP_ROM_PTR(&EVE_fgcolor_obj) },
    { MP_ROM_QSTR(MP_QSTR_bgcolor),             MP_ROM_PTR(&EVE_bgcolor_obj) },
    { MP_ROM_QSTR(MP_QSTR_alpha),               MP_ROM_PTR(&EVE_alpha_obj) },
    { MP_ROM_QSTR(MP_QSTR_alphafunc),           MP_ROM_PTR(&EVE_alphafunc_obj) },
    { MP_ROM_QSTR(MP_QSTR_colormask),           MP_ROM_PTR(&EVE_colormask_obj) },
    { MP_ROM_QSTR(MP_QSTR_blend),               MP_ROM_PTR(&EVE_blend_obj) },
    { MP_ROM_QSTR(MP_QSTR_stencilfunc),         MP_ROM_PTR(&EVE_stencilfunc_obj) },
    { MP_ROM_QSTR(MP_QSTR_stencilmask),         MP_ROM_PTR(&EVE_stencilmask_obj) },
    { MP_ROM_QSTR(MP_QSTR_stencilop),           MP_ROM_PTR(&EVE_stencilop_obj) },
    { MP_ROM_QSTR(MP_QSTR_gradient),            MP_ROM_PTR(&EVE_gradient_obj) },
    { MP_ROM_QSTR(MP_QSTR_image),               MP_ROM_PTR(&EVE_showimage_obj) },
    { MP_ROM_QSTR(MP_QSTR_setbitmap),           MP_ROM_PTR(&EVE_setbitmap_obj) },
    #ifdef CONFIG_EVE_CHIP_TYPE1
    { MP_ROM_QSTR(MP_QSTR_video),               MP_ROM_PTR(&EVE_playvideo_obj) },
    { MP_ROM_QSTR(MP_QSTR_closevideo),          MP_ROM_PTR(&EVE_closevideo_obj) },
    { MP_ROM_QSTR(MP_QSTR_videoframe),          MP_ROM_PTR(&EVE_videoframe_obj) },
    { MP_ROM_QSTR(MP_QSTR_lastframe),           MP_ROM_PTR(&EVE_lastframe_obj) },
    { MP_ROM_QSTR(MP_QSTR_videobuffer),         MP_ROM_PTR(&EVE_videobuffer_obj) },
    { MP_ROM_QSTR(MP_QSTR_rotate),              MP_ROM_PTR(&EVE_rotate_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_getprops),            MP_ROM_PTR(&EVE_getprops_obj) },
    { MP_ROM_QSTR(MP_QSTR_userfont),            MP_ROM_PTR(&EVE_userfont_obj) },
    { MP_ROM_QSTR(MP_QSTR_fontinfo),            MP_ROM_PTR(&EVE_fontinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_freeobjects),         MP_ROM_PTR(&EVE_freeobjects_obj) },
    { MP_ROM_QSTR(MP_QSTR_memWrite),            MP_ROM_PTR(&EVE_memWrite_obj) },
    { MP_ROM_QSTR(MP_QSTR_memRead),             MP_ROM_PTR(&EVE_memRead_obj) },
    { MP_ROM_QSTR(MP_QSTR_memZero),             MP_ROM_PTR(&EVE_memZero_obj) },
    { MP_ROM_QSTR(MP_QSTR_memSet),              MP_ROM_PTR(&EVE_memSet_obj) },
    { MP_ROM_QSTR(MP_QSTR_tag),                 MP_ROM_PTR(&EVE_tag_obj) },
    { MP_ROM_QSTR(MP_QSTR_tagmask),             MP_ROM_PTR(&EVE_tagmask_obj) },
    { MP_ROM_QSTR(MP_QSTR_gettag),              MP_ROM_PTR(&EVE_gettag_obj) },
    { MP_ROM_QSTR(MP_QSTR_gettagXY),            MP_ROM_PTR(&EVE_gettagXY_obj) },
    { MP_ROM_QSTR(MP_QSTR_touchXY),             MP_ROM_PTR(&EVE_touchXY_obj) },
    { MP_ROM_QSTR(MP_QSTR_vol_play),            MP_ROM_PTR(&EVE_vol_pb_obj) },
    { MP_ROM_QSTR(MP_QSTR_vol_sound),           MP_ROM_PTR(&EVE_vol_sound_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound),               MP_ROM_PTR(&EVE_sound_obj) },
    { MP_ROM_QSTR(MP_QSTR_screensize),          MP_ROM_PTR(&EVE_screensize_obj) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_Font),            MP_ROM_PTR(&font_eve_type) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Image),           MP_ROM_PTR(&image_eve_type) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Console),         MP_ROM_PTR(&console_eve_type) },
    #ifdef CONFIG_MICROPY_USE_TFT
    { MP_OBJ_NEW_QSTR(MP_QSTR_Tft),             MP_ROM_PTR(&tft_eve_type) },
    #endif

    // SPI bus constants
    { MP_ROM_QSTR(MP_QSTR_HSPI),                MP_ROM_INT(HSPI_HOST) },
    { MP_ROM_QSTR(MP_QSTR_VSPI),                MP_ROM_INT(VSPI_HOST) },

    // Options constants
    { MP_ROM_QSTR(MP_QSTR_OPT_CENTERX),         MP_ROM_INT(FT8_OPT_CENTERX) },
    { MP_ROM_QSTR(MP_QSTR_OPT_CENTERY),         MP_ROM_INT(FT8_OPT_CENTERY) },
    { MP_ROM_QSTR(MP_QSTR_OPT_CENTER),          MP_ROM_INT(FT8_OPT_CENTER) },
    { MP_ROM_QSTR(MP_QSTR_OPT_RIGHTX),          MP_ROM_INT(FT8_OPT_RIGHTX) },
    { MP_ROM_QSTR(MP_QSTR_OPT_LEFTX),           MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_OPT_FLAT),            MP_ROM_INT(FT8_OPT_FLAT) },
    { MP_ROM_QSTR(MP_QSTR_OPT_3D),              MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_OPT_MONO),            MP_ROM_INT(FT8_OPT_MONO) },
    { MP_ROM_QSTR(MP_QSTR_OPT_NODL),            MP_ROM_INT(FT8_OPT_NODL) },
    { MP_ROM_QSTR(MP_QSTR_OPT_SIGNED),          MP_ROM_INT(FT8_OPT_SIGNED) },
    { MP_ROM_QSTR(MP_QSTR_OPT_NOBACK),          MP_ROM_INT(FT8_OPT_NOBACK) },
    { MP_ROM_QSTR(MP_QSTR_OPT_NOTICKS),         MP_ROM_INT(FT8_OPT_NOTICKS) },
    { MP_ROM_QSTR(MP_QSTR_OPT_NOPOINTER),       MP_ROM_INT(FT8_OPT_NOPOINTER) },
    { MP_ROM_QSTR(MP_QSTR_OPT_NOHANDS),         MP_ROM_INT(FT8_OPT_NOHANDS) },
    { MP_ROM_QSTR(MP_QSTR_OPT_NOSECS),          MP_ROM_INT(FT8_OPT_NOSECS) },
    #ifdef CONFIG_EVE_CHIP_TYPE1
    { MP_ROM_QSTR(MP_QSTR_OPT_NOTEAR),          MP_ROM_INT(FT8_OPT_NOTEAR) },
    { MP_ROM_QSTR(MP_QSTR_OPT_FULLSCREEN),      MP_ROM_INT(FT8_OPT_FULLSCREEN) },
    { MP_ROM_QSTR(MP_QSTR_OPT_MEDIAFIFO),       MP_ROM_INT(FT8_OPT_MEDIAFIFO) },
    { MP_ROM_QSTR(MP_QSTR_OPT_SOUND),           MP_ROM_INT(FT8_OPT_SOUND) },
    { MP_ROM_QSTR(MP_QSTR_OPT_RGB565),          MP_ROM_INT(0) },
    #endif

    // Blend constants
    { MP_ROM_QSTR(MP_QSTR_ZERO),                MP_ROM_INT(FT8_ZERO) },
    { MP_ROM_QSTR(MP_QSTR_ONE),                 MP_ROM_INT(FT8_ONE) },
    { MP_ROM_QSTR(MP_QSTR_SRC_ALPHA),           MP_ROM_INT(FT8_SRC_ALPHA) },
    { MP_ROM_QSTR(MP_QSTR_DST_ALPHA),           MP_ROM_INT(FT8_DST_ALPHA) },
    { MP_ROM_QSTR(MP_QSTR_ONE_MINUS_SRC_ALPHA), MP_ROM_INT(FT8_ONE_MINUS_SRC_ALPHA) },
    { MP_ROM_QSTR(MP_QSTR_ONE_MINUS_DST_ALPHA), MP_ROM_INT(FT8_ONE_MINUS_DST_ALPHA) },

    // Bitmap Wrap & Filter constants
    { MP_ROM_QSTR(MP_QSTR_NEAREST),             MP_ROM_INT(FT8_NEAREST) },
    { MP_ROM_QSTR(MP_QSTR_BILINEAR),            MP_ROM_INT(FT8_BILINEAR) },
    { MP_ROM_QSTR(MP_QSTR_BORDER),              MP_ROM_INT(FT8_BORDER) },
    { MP_ROM_QSTR(MP_QSTR_REPEAT),              MP_ROM_INT(FT8_REPEAT) },

    // Bitmap format constants
    { MP_ROM_QSTR(MP_QSTR_IMG_ARGB1555),        MP_ROM_INT(FT8_ARGB1555) },
    { MP_ROM_QSTR(MP_QSTR_IMG_L1),              MP_ROM_INT(FT8_L1) },
    #ifdef CONFIG_EVE_CHIP_TYPE1
    { MP_ROM_QSTR(MP_QSTR_IMG_L2),              MP_ROM_INT(FT8_L2) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_IMG_L4),              MP_ROM_INT(FT8_L4) },
    { MP_ROM_QSTR(MP_QSTR_IMG_L8),              MP_ROM_INT(FT8_L8) },
    { MP_ROM_QSTR(MP_QSTR_IMG_RGB332),          MP_ROM_INT(FT8_RGB332) },
    { MP_ROM_QSTR(MP_QSTR_IMG_ARGB2),           MP_ROM_INT(FT8_ARGB2) },
    { MP_ROM_QSTR(MP_QSTR_IMG_ARGB4),           MP_ROM_INT(FT8_ARGB4) },
    { MP_ROM_QSTR(MP_QSTR_IMG_RGB565),          MP_ROM_INT(FT8_RGB565) },
    { MP_ROM_QSTR(MP_QSTR_BMP_TEXT8X8),         MP_ROM_INT(FT8_TEXT8X8) },
    { MP_ROM_QSTR(MP_QSTR_BMP_TEXTVGA),         MP_ROM_INT(FT8_TEXTVGA) },
    { MP_ROM_QSTR(MP_QSTR_BMP_PALETTED),        MP_ROM_INT(FT8_PALETTED) },
    { MP_ROM_QSTR(MP_QSTR_BMP_BARGRAPH),        MP_ROM_INT(FT8_BARGRAPH) },

    // Image type constants
    { MP_ROM_QSTR(MP_QSTR_IMG_PNG),             MP_ROM_INT(IMAGE_TYPE_PNG) },
    { MP_ROM_QSTR(MP_QSTR_IMG_JPG),             MP_ROM_INT(IMAGE_TYPE_JPG) },
    { MP_ROM_QSTR(MP_QSTR_IMG_RAW),             MP_ROM_INT(IMAGE_TYPE_RAW) },
    { MP_ROM_QSTR(MP_QSTR_IMG_COMPRESSED),      MP_ROM_INT(IMAGE_TYPE_BIN) },
    { MP_ROM_QSTR(MP_QSTR_IMG_AUTO),            MP_ROM_INT(IMAGE_TYPE_NONE) },

    // Color constants
    { MP_ROM_QSTR(MP_QSTR_BLACK),               MP_ROM_INT(EVE_BLACK) },
    { MP_ROM_QSTR(MP_QSTR_NAVY),                MP_ROM_INT(EVE_NAVY) },
    { MP_ROM_QSTR(MP_QSTR_DARKGREEN),           MP_ROM_INT(EVE_DARKGREEN) },
    { MP_ROM_QSTR(MP_QSTR_DARKCYAN),            MP_ROM_INT(EVE_DARKCYAN) },
    { MP_ROM_QSTR(MP_QSTR_MAROON),              MP_ROM_INT(EVE_MAROON) },
    { MP_ROM_QSTR(MP_QSTR_PURPLE),              MP_ROM_INT(EVE_PURPLE) },
    { MP_ROM_QSTR(MP_QSTR_OLIVE),               MP_ROM_INT(EVE_OLIVE) },
    { MP_ROM_QSTR(MP_QSTR_LIGHTGREY),           MP_ROM_INT(EVE_LIGHTGREY) },
    { MP_ROM_QSTR(MP_QSTR_DARKGREY),            MP_ROM_INT(EVE_DARKGREY) },
    { MP_ROM_QSTR(MP_QSTR_BLUE),                MP_ROM_INT(EVE_BLUE) },
    { MP_ROM_QSTR(MP_QSTR_GREEN),               MP_ROM_INT(EVE_GREEN) },
    { MP_ROM_QSTR(MP_QSTR_CYAN),                MP_ROM_INT(EVE_CYAN) },
    { MP_ROM_QSTR(MP_QSTR_RED),                 MP_ROM_INT(EVE_RED) },
    { MP_ROM_QSTR(MP_QSTR_MAGENTA),             MP_ROM_INT(EVE_MAGENTA) },
    { MP_ROM_QSTR(MP_QSTR_YELLOW),              MP_ROM_INT(EVE_YELLOW) },
    { MP_ROM_QSTR(MP_QSTR_WHITE),               MP_ROM_INT(EVE_WHITE) },
    { MP_ROM_QSTR(MP_QSTR_ORANGE),              MP_ROM_INT(EVE_ORANGE) },
    { MP_ROM_QSTR(MP_QSTR_GREENYELLOW),         MP_ROM_INT(EVE_GREENYELLOW) },
    { MP_ROM_QSTR(MP_QSTR_PINK),                MP_ROM_INT(EVE_PINK) },

    // Rotation constants
    { MP_ROM_QSTR(MP_QSTR_LANDSCAPE),           MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_LANDSCAPE_FLIP),      MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PORTRAIT),            MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_PORTRAIT_FLIP),       MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_MIRRORED),            MP_ROM_INT(4) },
};
STATIC MP_DEFINE_CONST_DICT(display_eve_locals_dict, display_eve_locals_dict_table);


//======================================
const mp_obj_type_t display_eve_type = {
    { &mp_type_type },
    .name = MP_QSTR_EVE,
    .print = display_eve_printinfo,
    .make_new = display_eve_make_new,
    .locals_dict = (mp_obj_t)&display_eve_locals_dict,
};

#endif // CONFIG_MICROPY_USE_EVE
