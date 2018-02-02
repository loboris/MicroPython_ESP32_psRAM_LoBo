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

#include <stdint.h>

/* switch over to FT81x */
#define FT8_81X_ENABLE


/* select the settings for the TFT attached */
#if 0
	#define FT8_VM800B35A
	#define FT8_VM800B43A
	#define FT8_VM800B50A
	#define FT8_FT810CB_HY50HD
	#define FT8_FT811CB_HY50HD
	#define FT8_ET07
	#define FT8_RVT70AQ
#endif

#define FT8_FT810CB_HY50HD


/* some pre-definded colors */
#define RED		0xff0000UL
#define ORANGE	0xffa500UL
#define GREEN	0x00ff00UL
#define BLUE	0x0000ffUL
#define YELLOW	0xffff00UL
/*#define PINK	0xff00ffUL*/
#define PURPLE	0x800080UL
#define WHITE	0xffffffUL
#define BLACK	0x000000UL


/* VM800B35A: FT800 320x240 3.5" FTDI */
#ifdef FT8_VM800B35A
#define FT8_VSYNC0	(0L)	/* Tvf Vertical Front Porch */
#define FT8_VSYNC1	(2L)	/* Tvf + Tvp Vertical Front Porch plus Vsync Pulse width */
#define FT8_VOFFSET	(13L)	/* Tvf + Tvp + Tvb Number of non-visible lines (in lines) */
#define FT8_VCYCLE	(263L)	/* Tv Total number of lines (visible and non-visible) (in lines) */
#define FT8_VSIZE	(240L)	/* Tvd Number of visible lines (in lines) - display height */
#define FT8_HSYNC0	(0L)	/* Thf Horizontal Front Porch */
#define FT8_HSYNC1	(10L)	/* Thf + Thp Horizontal Front Porch plus Hsync Pulse width */
#define FT8_HOFFSET 	(70L)	/* Thf + Thp + Thb Length of non-visible part of line (in PCLK cycles) */
#define FT8_HCYCLE 	(408L)	/* Th Total length of line (visible and non-visible) (in PCLKs) */
#define FT8_HSIZE	(320L)	/* Thd Length of visible part of line (in PCLKs) - display width */
#define FT8_PCLKPOL 	(0L)	/* PCLK polarity (0 = rising edge, 1 = falling edge) */
#define FT8_SWIZZLE 	(2L)	/* Defines the arrangement of the RGB pins of the FT800 */
#define FT8_PCLK		(8L)	/* 48MHz / REG_PCLK = PCLK frequency */
#define FT8_TOUCH_RZTHRESH (1200L)	/* touch-sensitivity */
#define FT8_HAS_CRYSTAL 1	/* use external crystal or internal oscillator? */
#endif

/* VM800B43A: FT800 480x272 4.3" FTDI */
#ifdef FT8_VM800B43A
#define FT8_VSYNC0	(0L)
#define FT8_VSYNC1	(10L)
#define FT8_VOFFSET	(12L)
#define FT8_VCYCLE	(292L)
#define FT8_VSIZE	(272L)
#define FT8_HSYNC0	(0L)
#define FT8_HSYNC1	(41L)
#define FT8_HOFFSET 	(43L)
#define FT8_HCYCLE 	(548L)
#define FT8_HSIZE	(480L)
#define FT8_PCLKPOL 	(1L)
#define FT8_SWIZZLE 	(0L)
#define FT8_PCLK		(5L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL 1
#endif

/* VM800B50A: FT800 480x272 5.0" FTDI */
#ifdef FT8_VM800B50A
#define FT8_VSYNC0	(0L)
#define FT8_VSYNC1	(10L)
#define FT8_VOFFSET	(12L)
#define FT8_VCYCLE	(292L)
#define FT8_VSIZE	(272L)
#define FT8_HSYNC0	(0L)
#define FT8_HSYNC1	(41L)
#define FT8_HOFFSET 	(43L)
#define FT8_HCYCLE 	(548L)
#define FT8_HSIZE	(480L)
#define FT8_PCLKPOL 	(1L)
#define FT8_SWIZZLE 	(0L)
#define FT8_PCLK		(5L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL 1
#endif

/* FT810CB-HY50HD: FT810 800x480 5" HAOYU */
#ifdef FT8_FT810CB_HY50HD
#define FT8_VSYNC0	(0L)
#define FT8_VSYNC1	(2L)
#define FT8_VOFFSET	(13L)
#define FT8_VCYCLE	(525L)
#define FT8_VSIZE	(480L)
#define FT8_HSYNC0	(0L)
#define FT8_HSYNC1	(20L)
#define FT8_HOFFSET 	(64L)
#define FT8_HCYCLE 	(952L)
#define FT8_HSIZE	(800L)
#define FT8_PCLKPOL 	(1L)
#define FT8_SWIZZLE 	(0L)
#define FT8_PCLK		(2L)
#define FT8_TOUCH_RZTHRESH (2000L)	/* touch-sensitivity */
#define FT8_HAS_CRYSTAL 1
#endif

/* FT811CB-HY50HD: FT811 800x480 5" HAOYU */
#ifdef FT8_FT811CB_HY50HD
#define FT8_VSYNC0	(0L)
#define FT8_VSYNC1	(2L)
#define FT8_VOFFSET	(13L)
#define FT8_VCYCLE	(525L)
#define FT8_VSIZE	(480L)
#define FT8_HSYNC0	(0L)
#define FT8_HSYNC1	(20L)
#define FT8_HOFFSET 	(64L)
#define FT8_HCYCLE 	(952L)
#define FT8_HSIZE	(800L)
#define FT8_PCLKPOL 	(1L)
#define FT8_SWIZZLE 	(0L)
#define FT8_PCLK		(2L)
#define FT8_TOUCH_RZTHRESH (1200L)	/* touch-sensitivity */
#define FT8_HAS_CRYSTAL 1
#endif

/* some test setup */
#ifdef FT8_800x480x
#define FT8_VSYNC0	(0L) /* Tvf Vertical Front Porch */
#define FT8_VSYNC1	(10L) /* Tvf + Tvp Vertical Front Porch plus Vsync Pulse width */
#define FT8_VOFFSET	(35L) /* Tvf + Tvp + Tvb Number of non-visible lines (in lines) */
#define FT8_VCYCLE	(516L)	/* Tv Total number of lines (visible and non-visible) (in lines) */
#define FT8_VSIZE	(480L)	/* Tvd Number of visible lines (in lines) - display height */
#define FT8_HSYNC0	(0L) /* (40L)	// Thf Horizontal Front Porch */
#define FT8_HSYNC1	(88L)	/* Thf + Thp Horizontal Front Porch plus Hsync Pulse width */
#define FT8_HOFFSET 	(169L) /* Thf + Thp + Thb Length of non-visible part of line (in PCLK cycles) */
#define FT8_HCYCLE 	(969L) /* Th Total length of line (visible and non-visible) (in PCLKs) */
#define FT8_HSIZE	(800L)	/* Thd Length of visible part of line (in PCLKs) - display width */
#define FT8_PCLKPOL 	(1L)	/* PCLK polarity (0 = rising edge, 1 = falling edge) */
#define FT8_SWIZZLE 	(0L)	/* Defines the arrangement of the RGB pins of the FT800 */
#define FT8_PCLK		(2L)	/* 60MHz / REG_PCLK = PCLK frequency	30 MHz */
#define FT8_TOUCH_RZTHRESH (1200L)	/* touch-sensitivity */
#define FT8_HAS_CRYSTAL 1
#endif

/* G-ET0700G0DM6 800x480 7" Glyn, untested */
#ifdef FT8_ET07
#define FT8_VSYNC0	(0L)
#define FT8_VSYNC1	(2L)
#define FT8_VOFFSET	(35L)
#define FT8_VCYCLE	(525L)
#define FT8_VSIZE	(480L)
#define FT8_HSYNC0	(0L)
#define FT8_HSYNC1	(128L)
#define FT8_HOFFSET 	(203L)
#define FT8_HCYCLE 	(1056L)
#define FT8_HSIZE	(800L)
#define FT8_PCLKPOL 	(1L)
#define FT8_SWIZZLE 	(0L)
#define FT8_PCLK		(2L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL 0	/* no idea if these come with a crystal populated or not */
#endif

/* RVT70AQxxxxxx 800x480 7" Riverdi, various options, FT812/FT813, tested with RVT70UQFNWC0x */
#ifdef FT8_RVT70AQ
#define FT8_VSYNC0	(0L)	/* Tvf Vertical Front Porch */
#define FT8_VSYNC1	(10L)	/* Tvf + Tvp Vertical Front Porch plus Vsync Pulse width */
#define FT8_VOFFSET	(23L)	/* Tvf + Tvp + Tvb Number of non-visible lines (in lines) */
#define FT8_VCYCLE	(525L)	/* Tv Total number of lines (visible and non-visible) (in lines) */
#define FT8_VSIZE	(480L)	/* Tvd Number of visible lines (in lines) - display height */
#define FT8_HSYNC0	(0L)	/* Thf Horizontal Front Porch */
#define FT8_HSYNC1	(10L)	/* Thf + Thp Horizontal Front Porch plus Hsync Pulse width */
#define FT8_HOFFSET 	(46L)	/* Thf + Thp + Thb Length of non-visible part of line (in PCLK cycles) */
#define FT8_HCYCLE 	(1056L)	/* Th Total length of line (visible and non-visible) (in PCLKs) */
#define FT8_HSIZE	(800L)	/* Thd Length of visible part of line (in PCLKs) - display width */
#define FT8_PCLKPOL 	(1L)	/* PCLK polarity (0 = rising edge, 1 = falling edge) */
#define FT8_SWIZZLE 	(0L)	/* Defines the arrangement of the RGB pins of the FT800 */
#define FT8_PCLK		(2L)	/* 60MHz / REG_PCLK = PCLK frequency 30 MHz */
#define FT8_TOUCH_RZTHRESH (1800L)	/* touch-sensitivity */
#define FT8_HAS_CRYSTAL 0
#endif

// ----------------------------------------------------------------------------------

extern uint16_t eve_cmdOffset;

void FT8_cmdWrite(uint8_t data);

uint8_t FT8_memRead8(uint32_t ftAddress);
uint16_t FT8_memRead16(uint32_t ftAddress);
uint32_t FT8_memRead32(uint32_t ftAddress);
void FT8_memWrite8(uint32_t ftAddress, uint8_t ftData8);
void FT8_memWrite16(uint32_t ftAddress, uint16_t ftData16);
void FT8_memWrite32(uint32_t ftAddress, uint32_t ftData32);
void FT8_memWrite_flash_buffer(uint32_t ftAddress, const uint8_t *data, uint16_t len);
uint8_t FT8_busy(void);
void FT8_cmd_dl(uint32_t command);

void FT8_get_cmdoffset(void);
uint32_t FT8_get_touch_tag(void);
void FT8_cmd_start(void);
void FT8_cmd_execute(void);

void FT8_start_cmd_burst(void);
void FT8_end_cmd_burst(void);

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

#ifdef FT8_81X_ENABLE
void FT8_cmd_setbase(uint32_t base);
void FT8_cmd_setbitmap(uint32_t addr, uint16_t fmt, uint16_t width, uint16_t height);
#endif

/* commands to operate on memory: */
void FT8_cmd_memzero(uint32_t ptr, uint32_t num);
void FT8_cmd_memset(uint32_t ptr, uint8_t value, uint32_t num);
/*(void FT8_cmd_memwrite(uint32_t dest, uint32_t num, const uint8_t *data); */
void FT8_cmd_memcpy(uint32_t dest, uint32_t src, uint32_t num);
void FT8_cmd_append(uint32_t ptr, uint32_t num);


/* commands for loading image data into FT8xx memory: */
void FT8_cmd_inflate(uint32_t ptr, const uint8_t *data, uint16_t len);
void FT8_cmd_loadimage(uint32_t ptr, uint32_t options, const uint8_t *data, uint16_t len);
#ifdef FT8_81X_ENABLE
void FT8_cmd_mediafifo(uint32_t ptr, uint32_t size);
#endif

/* commands for setting the bitmap transform matrix: */
void FT8_cmd_translate(int32_t tx, int32_t ty);
void FT8_cmd_scale(int32_t sx, int32_t sy);
void FT8_cmd_rotate(int32_t ang);
void FT8_cmd_getmatrix(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f);


/* other commands: */
void FT8_cmd_calibrate(void);
void FT8_cmd_interrupt(uint32_t ms);
void FT8_cmd_setfont(uint32_t font, uint32_t ptr);
#ifdef FT8_81X_ENABLE
void FT8_cmd_romfont(uint32_t font, uint32_t romslot);
void FT8_cmd_setfont2(uint32_t font, uint32_t ptr, uint32_t firstchar);
void FT8_cmd_setrotate(uint32_t r);
void FT8_cmd_setscratch(uint32_t handle);
#endif
void FT8_cmd_sketch(int16_t x0, int16_t y0, uint16_t w0, uint16_t h0, uint32_t ptr, uint16_t format);
void FT8_cmd_snapshot(uint32_t ptr);
#ifdef FT8_81X_ENABLE
void FT8_cmd_snapshot2(uint32_t fmt, uint32_t ptr, int16_t x0, int16_t y0, int16_t w0, int16_t h0);
#endif
void FT8_cmd_spinner(int16_t x0, int16_t y0, uint16_t style, uint16_t scale);
void FT8_cmd_track(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t tag);


/* commands that return values by writing to the command-fifo */
uint16_t FT8_cmd_memcrc(uint32_t ptr, uint32_t num);
uint16_t FT8_cmd_getptr(void);
uint16_t FT8_cmd_regread(uint32_t ptr);
uint16_t FT8_cmd_getprops(uint32_t ptr);


/* meta-commands, sequences of several display-list entries condensed into simpler to use functions at the price of some overhead */
void FT8_cmd_point(int16_t x0, int16_t y0, uint16_t size);
void FT8_cmd_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t w0);
void FT8_cmd_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t corner);


/* startup FT8xx: */
uint8_t FT8_init(void);

#endif // FT8_COMMANDS_H_
