/*
 * This file is part of the ESP32 MicroPython project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Contains Functions for using the FT8xx
 * === Ported from https://github.com/RudolphRiedel/FT800-FT813 ===
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Rudolph Riedel
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


#ifndef FT8_COMMANDS_H_
#define FT8_COMMANDS_H_

#include "sdkconfig.h"

#if CONFIG_MICROPY_USE_EVE

#include <stdint.h>
#include "esp_err.h"
//#include "tft/tftspi.h"
#include "driver/spi_master_utils.h"


#define MAX_USER_FONTS          15
//#define EVE_STATIC_LIST         0
//#define EVE_STATIC_LIST_SIZE    8192
//#define EVE_FONT_IMG_START      8192
#define EVE_FONT_METRICS_SIZE   148
#define EVE_FONT_WIDTHS_SIZE    128

#ifdef CONFIG_MICROPY_EVE_FT81X
#define FT8_81X_ENABLE
#endif

#define EVE_TYPE_NONE   0
#define EVE_TYPE_FT800  1
#define EVE_TYPE_FT801  2
#define EVE_TYPE_FT810  3
#define EVE_TYPE_FT811  4
#define EVE_TYPE_FT812  5
#define EVE_TYPE_FT813  6
#define EVE_TYPE_MAX    7

/* some pre-definded colors */
#define EVE_BLACK       0
#define EVE_NAVY        128
#define EVE_DARKGREEN   32768
#define EVE_DARKCYAN    32896
#define EVE_MAROON      8388608
#define EVE_PURPLE      8388736
#define EVE_OLIVE       8421376
#define EVE_LIGHTGREY   12632256
#define EVE_DARKGREY    8421504
#define EVE_BLUE        255
#define EVE_GREEN       65280
#define EVE_CYAN        65535
#define EVE_RED         16515072
#define EVE_MAGENTA     16515327
#define EVE_YELLOW      16579584
#define EVE_WHITE       16579836
#define EVE_ORANGE      16557056
#define EVE_GREENYELLOW 11336748
#define EVE_PINK        16564426


typedef struct {
    uint16_t vsync0;        // Tvf Vertical Front Porch
    uint16_t vsync1;        // Tvf + Tvp Vertical Front Porch plus Vsync Pulse width
    uint16_t voffset;       // Tvf + Tvp + Tvb Number of non-visible lines (in lines)
    uint16_t vcycle;        // Tv Total number of lines (visible and non-visible) (in lines)
    uint16_t vsize;         // Tvd Number of visible lines (in lines) - display height
    uint16_t hsync0;        // Thf Horizontal Front Porch
    uint16_t hsync1;        // Thf + Thp Horizontal Front Porch plus Hsync Pulse width
    uint16_t hoffset;       // Thf + Thp + Thb Length of non-visible part of line (in PCLK cycles)
    uint16_t hcycle;        // Th Total length of line (visible and non-visible) (in PCLKs)
    uint16_t hsize;         // Thd Length of visible part of line (in PCLKs) - display width
    uint8_t pclkpol;        // PCLK polarity (0 = rising edge, 1 = falling edge)
    uint8_t swizzle;        // Defines the arrangement of the RGB pins of the FT800
    uint8_t pclk;           // 48MHz or 60 MHz / REG_PCLK = PCLK frequency
    uint8_t has_crystal;    // use external crystal or internal oscillator?
    uint16_t touch_thresh;  // touch-sensitivity
    uint8_t cspread;        // helps with noise, when set to 1 fewer signals are changed simultaneously, reset-default: 1
    uint8_t has_GT911;      // special treatment required for out-of-spec touch-controller
} ft8_config_t;

// EVE Display structure
typedef struct {
    uint16_t        width;          // Display width (smaller dimension)
    uint16_t        height;         // Display height (larger dimension)
    uint32_t        speed;          // SPI clock in Hz
    uint8_t         host;           // SPI host (HSPI_HOST or VSPI_HOST)
    uint8_t         miso;           // SPI MISO pin
    uint8_t         mosi;           // SPI MOSI pin
    uint8_t         sck;            // SPI CLOCK pin
    uint8_t         cs;             // Display CS pin
    int8_t          pd;             // GPIO used as PD pin
    uint8_t         touch;          // Touch panel type
    ft8_config_t    disp_config;    // Pointer to display configuration structure
} eve_config_t;

typedef struct {
    uint32_t    fifo_buff;  //fifo buffer address
    int32_t     fifo_len;   //fifo length
    int32_t     fifo_wp;    //fifo write pointer - maintained by host
    int32_t     fifo_rp;    //fifo read point - maintained by device
    FILE        *pFile;
    int32_t     file_remain;
    int32_t     file_size;
    uint8_t     *g_scratch;
    uint16_t    fbuff_size;
    uint32_t    max_free;
    uint32_t    min_free;
} FT8_Fifo_t;


// ----------------------------------------------------------------------------------

extern uint16_t eve_chip_id;
extern exspi_device_handle_t *eve_spi;
extern uint16_t eve_cmdOffset;
extern uint32_t ft8_ramg_ptr;
extern FT8_Fifo_t ft8_stFifo;
extern uint8_t ft8_full_cs;
extern uint8_t eve_spibus_is_init;
extern uint8_t spi_is_init;


#ifdef CONFIG_EVE_CHIP_TYPE1
int FT8_Fifo_init(FILE *fhndl);
void FT8_Fifo_deinit();
#endif

void FT8_cmdWrite(uint8_t data);

uint8_t FT8_memRead8(uint32_t ftAddress);
uint16_t FT8_memRead16(uint32_t ftAddress);
uint32_t FT8_memRead32(uint32_t ftAddress);
void FT8_memRead_buffer(uint32_t ftAddress, uint8_t *data, uint16_t len);

void FT8_memWrite8(uint32_t ftAddress, uint8_t ftData8);
void FT8_memWrite16(uint32_t ftAddress, uint16_t ftData16);
void FT8_memWrite32(uint32_t ftAddress, uint32_t ftData32);
int FT8_memWrite_flash_buffer(uint32_t ftAddress, const uint8_t *data, uint16_t len, bool check_padding);

void FT8_send_long(uint32_t val1, uint32_t val2, uint32_t val3, uint8_t len);

uint8_t FT8_busy(void);
void FT8_cmd_dl(uint32_t command);

void FT8_inc_cmdoffset(uint16_t increment);
void FT8_get_cmdoffset(void);
uint32_t FT8_get_touch_tag(void);
void FT8_cmd_start(void);
bool FT8_cmd_execute(int tmo_ms);

void FT8_start_cmd_burst(void);
void FT8_end_cmd_burst(void);
void FT8_start_cmd(uint32_t command);


/* commands to draw graphics objects: */
void FT8_cmd_text(int16_t x0, int16_t y0, int16_t font, uint16_t options, const char* text);
void FT8_cmd_button(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t font, uint16_t options, const char* text);
void FT8_cmd_clock(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t hours, uint16_t minutes, uint16_t seconds, uint16_t millisecs);
void FT8_cmd_bgcolor(uint32_t color);
void FT8_cmd_fgcolor(uint32_t color);
void FT8_cmd_gradcolor(uint32_t color);
void FT8_cmd_gauge(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t major, uint16_t minor, uint16_t val, uint16_t range);
void FT8_cmd_gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1);
void FT8_cmd_keys(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t font, uint16_t options, const char* text);
void FT8_cmd_progress(int16_t x0, int16_t y0, int16_t w0, int16_t h0, uint16_t options, uint16_t val, uint16_t range);
void FT8_cmd_scrollbar(int16_t x0, int16_t y0, int16_t w0, int16_t h0, uint16_t options, uint16_t val, uint16_t size, uint16_t range);
void FT8_cmd_slider(int16_t x1, int16_t y1, int16_t w1, int16_t h1, uint16_t options, uint16_t val, uint16_t range);
void FT8_cmd_dial(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t val);
void FT8_cmd_toggle(int16_t x0, int16_t y0, int16_t w0, int16_t font, uint16_t options, uint16_t state, const char* text);
void FT8_cmd_number(int16_t x0, int16_t y0, int16_t font, uint16_t options, int32_t number);

void FT8_cmd_setbase(uint32_t base);
void FT8_cmd_setbitmap(uint32_t addr, uint16_t fmt, uint16_t width, uint16_t height);
void FT8_cmd_bitmapXY(uint16_t x, uint16_t y);

/* commands to operate on memory: */
void FT8_cmd_memzero(uint32_t ptr, uint32_t num);
void FT8_cmd_memset(uint32_t ptr, uint8_t value, uint32_t num);
void FT8_cmd_memwrite(uint32_t dest, const uint8_t *data, uint32_t num);
void FT8_cmd_memcpy(uint32_t dest, uint32_t src, uint32_t num);
void FT8_cmd_append(uint32_t ptr, uint32_t num);


/* commands for loading image data into FT8xx memory: */
void FT8_cmd_inflate(uint32_t ptr, const uint8_t *data, uint16_t len);
int FT8_cmd_loadimage(uint32_t ptr, uint32_t options, FILE *fhndl, uint32_t len, uint8_t type);

#ifdef CONFIG_EVE_CHIP_TYPE1
void FT8_cmd_mediafifo(uint32_t ptr, uint32_t size);

int FT8_sendDataViaMediafifo(FILE *pFile, uint32_t ptr, uint32_t options, uint8_t type);
int FT8_Fifo_service();
void FT8_cmd_videoframe(uint32_t addr);

void FT8_cmd_romfont(uint32_t font, uint32_t romslot);
void FT8_cmd_setfont2(uint32_t font, uint32_t ptr, uint32_t firstchar);
void FT8_cmd_setrotate(uint32_t r);
void FT8_cmd_setscratch(uint32_t handle);
#endif

/* commands for setting the bitmap transform matrix: */
void FT8_cmd_translate(int32_t tx, int32_t ty);
void FT8_cmd_scale(int32_t sx, int32_t sy);
void FT8_cmd_rotate(int32_t ang);
void FT8_cmd_getmatrix(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f);
void FT8_cmd_setmatrix();


/* other commands: */
void FT8_cmd_calibrate(void);
void FT8_cmd_interrupt(uint32_t ms);
void FT8_cmd_setfont(uint32_t font, uint32_t ptr);

void FT8_cmd_sketch(int16_t x0, int16_t y0, uint16_t w0, uint16_t h0, uint32_t ptr, uint16_t format);
void FT8_cmd_snapshot(uint32_t ptr);
void FT8_cmd_snapshot2(uint32_t fmt, uint32_t ptr, int16_t x0, int16_t y0, int16_t w0, int16_t h0);
void FT8_cmd_spinner(int16_t x0, int16_t y0, uint16_t style, uint16_t scale);
void FT8_cmd_track(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t tag);

void FT8_CP_reset();

/* commands that return values by writing to the command-fifo */
uint16_t FT8_cmd_memcrc(uint32_t ptr, uint32_t num);
uint16_t FT8_cmd_getptr(void);
uint16_t FT8_cmd_regread(uint32_t ptr);
uint16_t FT8_cmd_getprops(uint32_t ptr);


/* meta-commands, sequences of several display-list entries condensed into simpler to use functions at the price of some overhead */
void FT8_cmd_point(int16_t x0, int16_t y0, uint16_t size);
void FT8_cmd_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t w0);
void FT8_cmd_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t corner);
void FT8_cmd_strip(uint16_t *data, uint16_t length, uint8_t type, uint16_t width);


/* startup FT8xx: */
esp_err_t FT8_init(eve_config_t *dconfig, exspi_device_handle_t *disp_spi_dev);

#endif //CONFIG_MICROPY_USE_EVE

#endif // FT8_COMMANDS_H_
