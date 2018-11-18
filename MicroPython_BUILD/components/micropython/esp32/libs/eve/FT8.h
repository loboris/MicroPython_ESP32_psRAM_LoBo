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


#ifndef _FT8_H_
#define _FT8_H_

#include "sdkconfig.h"

#if CONFIG_MICROPY_USE_EVE

#include "FT8_commands.h"



#define DL_CLEAR		0x26000000UL /* requires OR'd arguments */
#define DL_CLEAR_RGB	0x02000000UL /* requires OR'd arguments */
#define DL_COLOR_RGB	0x04000000UL /* requires OR'd arguments */
#define DL_POINT_SIZE	0x0D000000UL /* requires OR'd arguments */
#define DL_END			0x21000000UL
#define DL_BEGIN		0x1F000000UL /* requires OR'd arguments */
#define DL_DISPLAY		0x00000000UL

#define CLR_COL              0x4
#define CLR_STN              0x2
#define CLR_TAG              0x1


/* Host commands */
#define FT8_ACTIVE	0x00  /* place FT8xx in active state */
#define FT8_STANDBY	0x41  /* place FT8xx in Standby (clk running) */
#define FT8_SLEEP	0x42  /* place FT8xx in Sleep (clk off) */
#define FT8_PWRDOWN	0x50  /* place FT8xx in Power Down (core off) */
#define FT8_CLKEXT	0x44  /* select external clock source */
#define FT8_CLKINT	0x48  /* select internal clock source */
#define FT8_CORERST	0x68  /* reset core - all registers default and processors reset */
#define FT8_CLK48M	0x62  /* select 48MHz PLL output */
#define FT8_CLK36M	0x61  /* select 36MHz PLL output */


/* defines used for graphics commands */
#define FT8_NEVER                0UL
#define FT8_LESS                 1UL
#define FT8_LEQUAL               2UL
#define FT8_GREATER              3UL
#define FT8_GEQUAL               4UL
#define FT8_EQUAL                5UL
#define FT8_NOTEQUAL             6UL
#define FT8_ALWAYS               7UL


/* Bitmap formats */
#define FT8_ARGB1555             0UL
#define FT8_L1                   1UL
#define FT8_L4                   2UL
#define FT8_L8                   3UL
#define FT8_RGB332               4UL
#define FT8_ARGB2                5UL
#define FT8_ARGB4                6UL
#define FT8_RGB565               7UL
#define FT8_PALETTED             8UL
#define FT8_TEXT8X8              9UL
#define FT8_TEXTVGA              10UL
#define FT8_BARGRAPH             11UL


/* Bitmap filter types */
#define FT8_NEAREST              0UL
#define FT8_BILINEAR             1UL


/* Bitmap wrap types */
#define FT8_BORDER               0UL
#define FT8_REPEAT               1UL


/* Stencil defines */
#define FT8_KEEP                 1UL
#define FT8_REPLACE              2UL
#define FT8_INCR                 3UL
#define FT8_DECR                 4UL
#define FT8_INVERT               5UL


/* Graphics display list swap defines */
#define FT8_DLSWAP_DONE          0UL
#define FT8_DLSWAP_LINE          1UL
#define FT8_DLSWAP_FRAME         2UL


/* Interrupt bits */
#define FT8_INT_SWAP             0x01
#define FT8_INT_TOUCH            0x02
#define FT8_INT_TAG              0x04
#define FT8_INT_SOUND            0x08
#define FT8_INT_PLAYBACK         0x10
#define FT8_INT_CMDEMPTY         0x20
#define FT8_INT_CMDFLAG          0x40
#define FT8_INT_CONVCOMPLETE     0x80


/* Touch mode */
#define FT8_TMODE_OFF        	0
#define FT8_TMODE_ONESHOT    	1
#define FT8_TMODE_FRAME      	2
#define FT8_TMODE_CONTINUOUS 	3


/* Alpha blending */
#define FT8_ZERO                 0UL
#define FT8_ONE                  1UL
#define FT8_SRC_ALPHA            2UL
#define FT8_DST_ALPHA            3UL
#define FT8_ONE_MINUS_SRC_ALPHA  4UL
#define FT8_ONE_MINUS_DST_ALPHA  5UL


/* Graphics primitives */
#define FT8_BITMAPS              1UL
#define FT8_POINTS               2UL
#define FT8_LINES                3UL
#define FT8_LINE_STRIP           4UL
#define FT8_EDGE_STRIP_R         5UL
#define FT8_EDGE_STRIP_L         6UL
#define FT8_EDGE_STRIP_A         7UL
#define FT8_EDGE_STRIP_B         8UL
#define FT8_RECTS                9UL


/* Widget command */
#define FT8_OPT_MONO             1
#define FT8_OPT_NODL             2
#define FT8_OPT_FLAT             256
#define FT8_OPT_CENTERX          512
#define FT8_OPT_CENTERY          1024
#define FT8_OPT_CENTER           (FT8_OPT_CENTERX | FT8_OPT_CENTERY)
#define FT8_OPT_NOBACK           4096
#define FT8_OPT_NOTICKS          8192
#define FT8_OPT_NOHM             16384
#define FT8_OPT_NOPOINTER        16384
#define FT8_OPT_NOSECS           32768
#define FT8_OPT_NOHANDS          49152
#define FT8_OPT_RIGHTX           2048
#define FT8_OPT_SIGNED           256


/* Defines related to inbuilt font */
#define FT8_NUMCHAR_PERFONT 		(128L)  /* number of font characters per bitmap handle */
#define FT8_FONT_TABLE_SIZE 		(148L)  /* size of the font table - utilized for loopup by the graphics engine */
#define FT8_FONT_TABLE_POINTER	(0xFFFFCUL) /* pointer to the inbuilt font tables starting from bitmap handle 16 */


/* Audio sample type defines */
#define FT8_LINEAR_SAMPLES       0UL	/* 8bit signed samples */
#define FT8_ULAW_SAMPLES         1UL	/* 8bit ulaw samples */
#define FT8_ADPCM_SAMPLES        2UL	/* 4bit ima adpcm samples */


/* Synthesized sound */
#define FT8_SILENCE              0x00
#define FT8_SQUAREWAVE           0x01
#define FT8_SINEWAVE             0x02
#define FT8_SAWTOOTH             0x03
#define FT8_TRIANGLE             0x04
#define FT8_BEEPING              0x05
#define FT8_ALARM                0x06
#define FT8_WARBLE               0x07
#define FT8_CAROUSEL             0x08
#define FT8_PIPS(n)              (0x0F + (n))
#define FT8_HARP                 0x40
#define FT8_XYLOPHONE            0x41
#define FT8_TUBA                 0x42
#define FT8_GLOCKENSPIEL         0x43
#define FT8_ORGAN                0x44
#define FT8_TRUMPET              0x45
#define FT8_PIANO                0x46
#define FT8_CHIMES               0x47
#define FT8_MUSICBOX             0x48
#define FT8_BELL                 0x49
#define FT8_CLICK                0x50
#define FT8_SWITCH               0x51
#define FT8_COWBELL              0x52
#define FT8_NOTCH                0x53
#define FT8_HIHAT                0x54
#define FT8_KICKDRUM             0x55
#define FT8_POP                  0x56
#define FT8_CLACK                0x57
#define FT8_CHACK                0x58
#define FT8_MUTE                 0x60
#define FT8_UNMUTE               0x61


/* Synthesized sound frequencies, midi note */
#define FT8_MIDI_A0   21
#define FT8_MIDI_A_0  22
#define FT8_MIDI_B0   23
#define FT8_MIDI_C1   24
#define FT8_MIDI_C_1  25
#define FT8_MIDI_D1   26
#define FT8_MIDI_D_1  27
#define FT8_MIDI_E1   28
#define FT8_MIDI_F1   29
#define FT8_MIDI_F_1  30
#define FT8_MIDI_G1   31
#define FT8_MIDI_G_1  32
#define FT8_MIDI_A1   33
#define FT8_MIDI_A_1  34
#define FT8_MIDI_B1   35
#define FT8_MIDI_C2   36
#define FT8_MIDI_C_2  37
#define FT8_MIDI_D2   38
#define FT8_MIDI_D_2  39
#define FT8_MIDI_E2   40
#define FT8_MIDI_F2   41
#define FT8_MIDI_F_2  42
#define FT8_MIDI_G2   43
#define FT8_MIDI_G_2  44
#define FT8_MIDI_A2   45
#define FT8_MIDI_A_2  46
#define FT8_MIDI_B2   47
#define FT8_MIDI_C3   48
#define FT8_MIDI_C_3  49
#define FT8_MIDI_D3   50
#define FT8_MIDI_D_3  51
#define FT8_MIDI_E3   52
#define FT8_MIDI_F3   53
#define FT8_MIDI_F_3  54
#define FT8_MIDI_G3   55
#define FT8_MIDI_G_3  56
#define FT8_MIDI_A3   57
#define FT8_MIDI_A_3  58
#define FT8_MIDI_B3   59
#define FT8_MIDI_C4   60
#define FT8_MIDI_C_4  61
#define FT8_MIDI_D4   62
#define FT8_MIDI_D_4  63
#define FT8_MIDI_E4   64
#define FT8_MIDI_F4   65
#define FT8_MIDI_F_4  66
#define FT8_MIDI_G4   67
#define FT8_MIDI_G_4  68
#define FT8_MIDI_A4   69
#define FT8_MIDI_A_4  70
#define FT8_MIDI_B4   71
#define FT8_MIDI_C5   72
#define FT8_MIDI_C_5  73
#define FT8_MIDI_D5   74
#define FT8_MIDI_D_5  75
#define FT8_MIDI_E5   76
#define FT8_MIDI_F5   77
#define FT8_MIDI_F_5  78
#define FT8_MIDI_G5   79
#define FT8_MIDI_G_5  80
#define FT8_MIDI_A5   81
#define FT8_MIDI_A_5  82
#define FT8_MIDI_B5   83
#define FT8_MIDI_C6   84
#define FT8_MIDI_C_6  85
#define FT8_MIDI_D6   86
#define FT8_MIDI_D_6  87
#define FT8_MIDI_E6   88
#define FT8_MIDI_F6   89
#define FT8_MIDI_F_6  90
#define FT8_MIDI_G6   91
#define FT8_MIDI_G_6  92
#define FT8_MIDI_A6   93
#define FT8_MIDI_A_6  94
#define FT8_MIDI_B6   95
#define FT8_MIDI_C7   96
#define FT8_MIDI_C_7  97
#define FT8_MIDI_D7   98
#define FT8_MIDI_D_7  99
#define FT8_MIDI_E7   100
#define FT8_MIDI_F7   101
#define FT8_MIDI_F_7  102
#define FT8_MIDI_G7   103
#define FT8_MIDI_G_7  104
#define FT8_MIDI_A7   105
#define FT8_MIDI_A_7  106
#define FT8_MIDI_B7   107
#define FT8_MIDI_C8   108


/* GPIO bits */
#define FT8_GPIO0	0
#define FT8_GPIO1	1	/* default gpio pin for audio shutdown, 1 - eanble, 0 - disable */
#define FT8_GPIO7	7	/* default gpio pin for display enable, 1 - enable, 0 - disable */


/* Display rotation */
#define FT8_DISPLAY_0		0	/* 0 degrees rotation */
#define FT8_DISPLAY_180		1	/* 180 degrees rotation */


/* Coprocessor related commands */
#define CMD_APPEND				0xFFFFFF1EUL
#define CMD_BGCOLOR				0xFFFFFF09UL
#define CMD_BUTTON				0xFFFFFF0DUL
#define CMD_CALIBRATE			0xFFFFFF15UL
#define CMD_CLOCK				0xFFFFFF14UL
#define CMD_COLDSTART			0xFFFFFF32UL
#define CMD_DIAL				0xFFFFFF2DUL
#define CMD_DLSTART				0xFFFFFF00UL
#define CMD_FGCOLOR				0xFFFFFF0AUL
#define CMD_GAUGE				0xFFFFFF13UL
#define CMD_GETMATRIX			0xFFFFFF33UL
#define CMD_GETPROPS			0xFFFFFF25UL
#define CMD_GETPTR				0xFFFFFF23UL
#define CMD_GRADCOLOR			0xFFFFFF34UL
#define CMD_GRADIENT			0xFFFFFF0BUL
#define CMD_INFLATE				0xFFFFFF22UL
#define CMD_INTERRUPT			0xFFFFFF02UL
#define CMD_KEYS				0xFFFFFF0EUL
#define CMD_LOADIDENTITY		0xFFFFFF26UL
#define CMD_LOADIMAGE			0xFFFFFF24UL
#define CMD_LOGO				0xFFFFFF31UL
#define CMD_MEMCPY				0xFFFFFF1DUL
#define CMD_MEMCRC				0xFFFFFF18UL
#define CMD_MEMSET				0xFFFFFF1BUL
#define CMD_MEMWRITE			0xFFFFFF1AUL
#define CMD_MEMZERO				0xFFFFFF1CUL
#define CMD_NUMBER				0xFFFFFF2EUL
#define CMD_PROGRESS			0xFFFFFF0FUL
#define CMD_REGREAD				0xFFFFFF19UL
#define CMD_ROTATE				0xFFFFFF29UL
#define CMD_SCALE				0xFFFFFF28UL
#define CMD_SCREENSAVER			0xFFFFFF2FUL
#define CMD_SCROLLBAR			0xFFFFFF11UL
#define CMD_SETFONT				0xFFFFFF2BUL
#define CMD_SETMATRIX			0xFFFFFF2AUL
#define CMD_SKETCH				0xFFFFFF30UL
#define CMD_SLIDER				0xFFFFFF10UL
#define CMD_SNAPSHOT			0xFFFFFF1FUL
#define CMD_SPINNER				0xFFFFFF16UL
#define CMD_STOP				0xFFFFFF17UL
#define CMD_SWAP				0xFFFFFF01UL
#define CMD_TEXT				0xFFFFFF0CUL
#define CMD_TOGGLE				0xFFFFFF12UL
#define CMD_TRACK				0xFFFFFF2CUL
#define CMD_TRANSLATE			0xFFFFFF27UL


/* the following are undocumented commands that therefore should not be used */
#if 0
#define CMD_CRC					0xFFFFFF03UL
#define CMD_HAMMERAUX			0xFFFFFF04UL
#define CMD_MARCH				0xFFFFFF05UL
#define CMD_IDCT				0xFFFFFF06UL
#define CMD_EXECUTE				0xFFFFFF07UL
#define CMD_GETPOINT			0xFFFFFF08UL
#define CMD_TOUCH_TRANSFORM		0xFFFFFF20UL
#define CMD_BITMAP_TRANSFORM	0xFFFFFF21UL
#endif


/* FT8xx graphics engine specific macros useful for static display list generation */
#define ALPHA_FUNC(func,ref) ((9UL<<24)|(((func)&7UL)<<8)|(((ref)&255UL)<<0))
#define BEGIN(prim) ((31UL<<24)|(((prim)&15UL)<<0))
#define BITMAP_HANDLE(handle) ((5UL<<24)|(((handle)&31UL)<<0))
#define BITMAP_LAYOUT(format,linestride,height) ((7UL<<24)|(((format)&31UL)<<19)|(((linestride)&1023UL)<<9)|(((height)&511UL)<<0))
#define BITMAP_SIZE(filter,wrapx,wrapy,width,height) ((8UL<<24)|(((filter)&1UL)<<20)|(((wrapx)&1UL)<<19)|(((wrapy)&1UL)<<18)|(((width)&511UL)<<9)|(((height)&511UL)<<0))
#define BITMAP_TRANSFORM_A(a) ((21UL<<24)|(((a)&131071UL)<<0))
#define BITMAP_TRANSFORM_B(b) ((22UL<<24)|(((b)&131071UL)<<0))
#define BITMAP_TRANSFORM_C(c) ((23UL<<24)|(((c)&16777215UL)<<0))
#define BITMAP_TRANSFORM_D(d) ((24UL<<24)|(((d)&131071UL)<<0))
#define BITMAP_TRANSFORM_E(e) ((25UL<<24)|(((e)&131071UL)<<0))
#define BITMAP_TRANSFORM_F(f) ((26UL<<24)|(((f)&16777215UL)<<0))
#define BLEND_FUNC(src,dst) ((11UL<<24)|(((src)&7UL)<<3)|(((dst)&7UL)<<0))
#define CALL(dest) ((29UL<<24)|(((dest)&65535UL)<<0))
#define CELL(cell) ((6UL<<24)|(((cell)&127UL)<<0))
#define CLEAR(c,s,t) ((38UL<<24)|(((c)&1UL)<<2)|(((s)&1UL)<<1)|(((t)&1UL)<<0))
#define CLEAR_COLOR_A(alpha) ((15UL<<24)|(((alpha)&255UL)<<0))
#define CLEAR_COLOR_RGB(red,green,blue) ((2UL<<24)|(((red)&255UL)<<16)|(((green)&255UL)<<8)|(((blue)&255UL)<<0))
#define CLEAR_STENCIL(s) ((17UL<<24)|(((s)&255UL)<<0))
#define CLEAR_TAG(s) ((18UL<<24)|(((s)&255UL)<<0))
#define COLOR_A(alpha) ((16UL<<24)|(((alpha)&255UL)<<0))
#define COLOR_MASK(r,g,b,a) ((32UL<<24)|(((r)&1UL)<<3)|(((g)&1UL)<<2)|(((b)&1UL)<<1)|(((a)&1UL)<<0))
#define COLOR_RGB(red,green,blue) ((4UL<<24)|(((red)&255UL)<<16)|(((green)&255UL)<<8)|(((blue)&255UL)<<0))
/* #define DISPLAY() ((0UL<<24)) */
#define END() ((33UL<<24))
#define JUMP(dest) ((30UL<<24)|(((dest)&65535UL)<<0))
#define LINE_WIDTH(width) ((14UL<<24)|(((width)&4095UL)<<0))
#define MACRO(m) ((37UL<<24)|(((m)&1UL)<<0))
#define POINT_SIZE(size) ((13UL<<24)|(((size)&8191UL)<<0))
#define RESTORE_CONTEXT() ((35UL<<24))
#define RETURN() ((36UL<<24))
#define SAVE_CONTEXT() ((34UL<<24))
#define STENCIL_FUNC(func,ref,mask) ((10UL<<24)|(((func)&7UL)<<16)|(((ref)&255UL)<<8)|(((mask)&255UL)<<0))
#define STENCIL_MASK(mask) ((19UL<<24)|(((mask)&255UL)<<0))
#define STENCIL_OP(sfail,spass) ((12UL<<24)|(((sfail)&7UL)<<3)|(((spass)&7UL)<<0))
#define TAG(s) ((3UL<<24)|(((s)&255UL)<<0))
#define TAG_MASK(mask) ((20UL<<24)|(((mask)&1UL)<<0))
#define VERTEX2F(x,y) ((1UL<<30)|(((x)&32767UL)<<15)|(((y)&32767UL)<<0))
#define VERTEX2II(x,y,handle,cell) ((2UL<<30)|(((x)&511UL)<<21)|(((y)&511UL)<<12)|(((handle)&31UL)<<7)|(((cell)&127UL)<<0))


#define REG_ID_FT81X    0x302000UL
#define REG_ID_FT80X    0x102400UL

/* specific for FT80x */
#ifdef CONFIG_EVE_CHIP_TYPE0

#define FT8_CHIPID		0x00010008UL

/* Coprocessor reset related */
#define FT8_RESET_HOLD_COPROCESSOR		1
#define FT8_RESET_RELEASE_COPROCESSOR	0

/* Maximum display display resolution supported by graphics engine */
#define FT8_MAX_DISPLAYWIDTH	(512L)
#define FT8_MAX_DISPLAYHEIGHT	(512L)

/* Defines for sound play and stop */
#define FT8_SOUND_PLAY	1
#define FT8_AUDIO_PLAY	1

/* Defines for audio playback parameters */
#define FT8_AUDIO_SAMPLINGFREQ_MIN	8*1000L
#define FT8_AUDIO_SAMPLINGFREQ_MAX	48*1000L

/* coprocessor error */
#define FT8_COPRO_ERROR			0xfffUL

/* Memory definitions */
#define FT8_RAM_G		0x000000UL
#define FT8_ROM_CHIPID	0x0C0000UL
#define FT8_ROM_FONT		0x0BB23CUL
#define FT8_ROM_FONT_ADDR	0x0FFFFCUL
#define FT8_RAM_DL		0x100000UL
#define FT8_RAM_PAL		0x102000UL
#define FT8_RAM_CMD		0x108000UL
#define FT8_RAM_SCREENSHOT	0x1C2000UL

/* Memory buffer sizes */
#define FT8_RAM_G_SIZE		256*1024L
#define FT8_CMDFIFO_SIZE		4*1024L
#define FT8_RAM_DL_SIZE		8*1024L
#define FT8_RAM_PAL_SIZE		1*1024L

/* Register definitions */
#define REG_ID					0x102400UL
#define REG_FRAMES				0x102404UL
#define REG_CLOCK				0x102408UL
#define REG_FREQUENCY			0x10240CUL
#define REG_SCREENSHOT_EN		0x102410UL
#define REG_SCREENSHOT_Y		0x102414UL
#define REG_SCREENSHOT_START	0x102418UL
#define REG_CPURESET 			0x10241CUL
#define REG_TAP_CRC 			0x102420UL
#define REG_TAP_MASK 			0x102424UL
#define REG_HCYCLE 				0x102428UL
#define REG_HOFFSET 			0x10242CUL
#define REG_HSIZE 				0x102430UL
#define REG_HSYNC0 				0x102434UL
#define REG_HSYNC1 				0x102438UL
#define REG_VCYCLE 				0x10243CUL
#define REG_VOFFSET 			0x102440UL
#define REG_VSIZE 				0x102444UL
#define REG_VSYNC0 				0x102448UL
#define REG_VSYNC1 				0x10244CUL
#define REG_DLSWAP 				0x102450UL
#define REG_ROTATE 				0x102454UL
#define REG_OUTBITS 			0x102458UL
#define REG_DITHER 				0x10245CUL
#define REG_SWIZZLE 			0x102460UL
#define REG_CSPREAD 			0x102464UL
#define REG_PCLK_POL 			0x102468UL
#define REG_PCLK 				0x10246CUL
#define REG_TAG_X 				0x102470UL
#define REG_TAG_Y 				0x102474UL
#define REG_TAG 				0x102478UL
#define REG_VOL_PB 				0x10247CUL
#define REG_VOL_SOUND 			0x102480UL
#define REG_SOUND 				0x102484UL
#define REG_PLAY 				0x102488UL
#define REG_GPIO_DIR 			0x10248CUL
#define REG_GPIO 				0x102490UL
#define REG_INT_FLAGS       	0x102498UL
#define REG_INT_EN          	0x10249CUL
#define REG_INT_MASK        	0x1024A0UL
#define REG_PLAYBACK_START  	0x1024A4UL
#define REG_PLAYBACK_LENGTH  	0x1024A8UL
#define REG_PLAYBACK_READPTR 	0x1024ACUL
#define REG_PLAYBACK_FREQ    	0x1024B0UL
#define REG_PLAYBACK_FORMAT  	0x1024B4UL
#define REG_PLAYBACK_LOOP    	0x1024B8UL
#define REG_PLAYBACK_PLAY   	0x1024BCUL
#define REG_PWM_HZ          	0x1024C0UL
#define REG_PWM_DUTY        	0x1024C4UL
#define REG_MACRO_0         	0x1024C8UL
#define REG_MACRO_1         	0x1024CCUL
#define REG_SCREENSHOT_BUSY		0x1024D8UL
#define REG_CMD_READ         	0x1024E4UL
#define REG_CMD_WRITE        	0x1024E8UL
#define REG_CMD_DL           	0x1024ECUL
#define REG_TOUCH_MODE       	0x1024F0UL
#define REG_TOUCH_ADC_MODE   	0x1024F4UL
#define REG_TOUCH_CHARGE     	0x1024F8UL
#define REG_TOUCH_SETTLE     	0x1024FCUL
#define REG_TOUCH_OVERSAMPLE 	0x102500UL
#define REG_TOUCH_RZTHRESH   	0x102504UL
#define REG_TOUCH_RAW_XY     	0x102508UL
#define REG_TOUCH_RZ         	0x10250CUL
#define REG_TOUCH_SCREEN_XY  	0x102510UL
#define REG_TOUCH_TAG_XY     	0x102514UL
#define REG_TOUCH_TAG        	0x102518UL
#define REG_TOUCH_TRANSFORM_A	0x10251CUL
#define REG_TOUCH_TRANSFORM_B	0x102520UL
#define REG_TOUCH_TRANSFORM_C	0x102524UL
#define REG_TOUCH_TRANSFORM_D	0x102528UL
#define REG_TOUCH_TRANSFORM_E	0x10252CUL
#define REG_TOUCH_TRANSFORM_F	0x102530UL
#define REG_SCREENSHOT_READ		0x102554UL
#define REG_TRIM				0x10256CUL
#define REG_TOUCH_DIRECT_XY 	0x102574UL
#define REG_TOUCH_DIRECT_Z1Z2	0x102578UL
#define REG_TRACKER				0x109000UL

/* FT80x graphics engine specific macros useful for static display list generation */
#define BITMAP_SOURCE(addr) ((1UL<<24)|(((addr)&1048575UL)<<0))
#define SCISSOR_SIZE(width,height) ((28UL<<24)|(((width)&1023UL)<<10)|(((height)&1023UL)<<0))
#define SCISSOR_XY(x,y) ((27UL<<24)|(((x)&511UL)<<9)|(((y)&511UL)<<0))


/* FT81x */
#else

#define LOW_FREQ_BOUND  58800000L /* 98% of 60Mhz */

/* Memory definitions */
#define FT8_RAM_G			0x000000UL
#define FT8_ROM_CHIPID		0x0C0000UL
#define FT8_ROM_FONT		0x1E0000UL
#define FT8_ROM_FONT_ADDR	0x2FFFFCUL
#define FT8_RAM_DL			0x300000UL
#define FT8_RAM_REG			0x302000UL
#define FT8_RAM_CMD			0x308000UL

/* Memory buffer sizes */
#define FT8_RAM_G_SIZE		1024*1024L
#define FT8_CMDFIFO_SIZE	4*1024L
#define FT8_RAM_DL_SIZE		8*1024L


/* various additional defines for FT81x */
#define FT8_ADC_DIFFERENTIAL     1UL
#define FT8_ADC_SINGLE_ENDED     0UL

#define FT8_INT_G8               18UL
#define FT8_INT_L8C              12UL
#define FT8_INT_VGA              13UL

#define FT8_OPT_MEDIAFIFO        16UL
#define FT8_OPT_FULLSCREEN       8UL
#define FT8_OPT_NOTEAR           4UL
#define FT8_OPT_SOUND            32UL

#define FT8_PALETTED4444         15UL
#define FT8_PALETTED565          14UL
#define FT8_PALETTED8            16UL
#define FT8_L2                   17UL


/* additional commands for FT81x */
#define CMD_MEDIAFIFO			0xFFFFFF39UL
#define CMD_PLAYVIDEO			0xFFFFFF3AUL
#define CMD_ROMFONT				0xFFFFFF3FU
#define CMD_SETBASE				0xFFFFFF38UL
#define CMD_SETBITMAP			0xFFFFFF43UL
#define CMD_SETFONT2			0xFFFFFF3BUL
#define CMD_SETROTATE			0xFFFFFF36UL
#define CMD_SETSCRATCH			0xFFFFFF3CUL
#define CMD_SNAPSHOT2			0xFFFFFF37UL
#define CMD_VIDEOFRAME			0xFFFFFF41UL
#define CMD_VIDEOSTART			0xFFFFFF40UL


/* the following are undocumented commands that therefore should not be used */
#if 0
#define CMD_CSKETCH				0xFFFFFF35UL
#define CMD_INT_RAMSHARED		0xFFFFFF3DUL
#define CMD_INT_SWLOADIMAGE		0xFFFFFF3EUL
#define CMD_SYNC				0xFFFFFF42UL
#endif


/* Register definitions */
#define REG_ANA_COMP         0x302184UL /* only listed in datasheet */
#define REG_BIST_EN          0x302174UL /* only listed in datasheet */
#define REG_CLOCK            0x302008UL
#define REG_CMDB_SPACE       0x302574UL
#define REG_CMDB_WRITE       0x302578UL
#define REG_CMD_DL           0x302100UL
#define REG_CMD_READ         0x3020f8UL
#define REG_CMD_WRITE        0x3020fcUL
#define REG_CPURESET         0x302020UL
#define REG_CSPREAD          0x302068UL
#define REG_CTOUCH_EXTENDED  0x302108UL
#define REG_CTOUCH_TOUCH0_XY 0x302124UL /* only listed in datasheet */
#define REG_CTOUCH_TOUCH4_X  0x30216cUL
#define REG_CTOUCH_TOUCH4_Y  0x302120UL
#define REG_CTOUCH_TOUCH1_XY 0x30211cUL
#define REG_CTOUCH_TOUCH2_XY 0x30218cUL
#define REG_CTOUCH_TOUCH3_XY 0x302190UL
#define REG_TOUCH_CONFIG     0x302168UL
#define REG_DATESTAMP        0x302564UL /* only listed in datasheet */
#define REG_DITHER           0x302060UL
#define REG_DLSWAP           0x302054UL
#define REG_FRAMES           0x302004UL
#define REG_FREQUENCY        0x30200cUL
#define REG_GPIO             0x302094UL
#define REG_GPIOX            0x30209cUL
#define REG_GPIOX_DIR        0x302098UL
#define REG_GPIO_DIR         0x302090UL
#define REG_HCYCLE           0x30202cUL
#define REG_HOFFSET          0x302030UL
#define REG_HSIZE            0x302034UL
#define REG_HSYNC0           0x302038UL
#define REG_HSYNC1           0x30203cUL
#define REG_ID               0x302000UL
#define REG_INT_EN           0x3020acUL
#define REG_INT_FLAGS        0x3020a8UL
#define REG_INT_MASK         0x3020b0UL
#define REG_MACRO_0          0x3020d8UL
#define REG_MACRO_1          0x3020dcUL
#define REG_MEDIAFIFO_READ   0x309014UL /* only listed in programmers guide */
#define REG_MEDIAFIFO_WRITE  0x309018UL /* only listed in programmers guide */
#define REG_OUTBITS          0x30205cUL
#define REG_PCLK             0x302070UL
#define REG_PCLK_POL         0x30206cUL
#define REG_PLAY             0x30208cUL
#define REG_PLAYBACK_FORMAT  0x3020c4UL
#define REG_PLAYBACK_FREQ    0x3020c0UL
#define REG_PLAYBACK_LENGTH  0x3020b8UL
#define REG_PLAYBACK_LOOP    0x3020c8UL
#define REG_PLAYBACK_PLAY    0x3020ccUL
#define REG_PLAYBACK_READPTR 0x3020bcUL
#define REG_PLAYBACK_START   0x3020b4UL
#define REG_PWM_DUTY         0x3020d4UL
#define REG_PWM_HZ           0x3020d0UL
#define REG_RENDERMODE       0x302010UL /* only listed in datasheet */
#define REG_ROTATE           0x302058UL
#define REG_SNAPFORMAT       0x30201cUL /* only listed in datasheet */
#define REG_SNAPSHOT         0x302018UL /* only listed in datasheet */
#define REG_SNAPY            0x302014UL /* only listed in datasheet */
#define REG_SOUND            0x302088UL
#define REG_SPI_WIDTH        0x302188UL /* listed with false offset in programmers guide V1.1 */
#define REG_SWIZZLE          0x302064UL
#define REG_TAG              0x30207cUL
#define REG_TAG_X            0x302074UL
#define REG_TAG_Y            0x302078UL
#define REG_TAP_CRC          0x302024UL /* only listed in datasheet */
#define REG_TAP_MASK         0x302028UL /* only listed in datasheet */
#define REG_TOUCH_ADC_MODE   0x302108UL
#define REG_TOUCH_CHARGE     0x30210cUL
#define REG_TOUCH_DIRECT_XY  0x30218cUL
#define REG_TOUCH_DIRECT_Z1Z2 0x302190UL
#define REG_TOUCH_MODE       0x302104UL
#define REG_TOUCH_OVERSAMPLE 0x302114UL
#define REG_TOUCH_RAW_XY     0x30211cUL
#define REG_TOUCH_RZ         0x302120UL
#define REG_TOUCH_RZTHRESH   0x302118UL
#define REG_TOUCH_SCREEN_XY  0x302124UL
#define REG_TOUCH_SETTLE     0x302110UL
#define REG_TOUCH_TAG        0x30212cUL
#define REG_TOUCH_TAG1       0x302134UL /* only listed in datasheet */
#define REG_TOUCH_TAG1_XY    0x302130UL /* only listed in datasheet */
#define REG_TOUCH_TAG2       0x30213cUL /* only listed in datasheet */
#define REG_TOUCH_TAG2_XY    0x302138UL /* only listed in datasheet */
#define REG_TOUCH_TAG3       0x302144UL /* only listed in datasheet */
#define REG_TOUCH_TAG3_XY    0x302140UL /* only listed in datasheet */
#define REG_TOUCH_TAG4       0x30214cUL /* only listed in datasheet */
#define REG_TOUCH_TAG4_XY    0x302148UL /* only listed in datasheet */
#define REG_TOUCH_TAG_XY     0x302128UL
#define REG_TOUCH_TRANSFORM_A 0x302150UL
#define REG_TOUCH_TRANSFORM_B 0x302154UL
#define REG_TOUCH_TRANSFORM_C 0x302158UL
#define REG_TOUCH_TRANSFORM_D 0x30215cUL
#define REG_TOUCH_TRANSFORM_E 0x302160UL
#define REG_TOUCH_TRANSFORM_F 0x302164UL
#define REG_TRACKER          0x309000UL /* only listed in programmers guide */
#define REG_TRACKER_1        0x309004UL /* only listed in programmers guide */
#define REG_TRACKER_2        0x309008UL /* only listed in programmers guide */
#define REG_TRACKER_3        0x30900cUL /* only listed in programmers guide */
#define REG_TRACKER_4        0x309010UL /* only listed in programmers guide */
#define REG_TRIM             0x302180UL
#define REG_VCYCLE           0x302040UL
#define REG_VOFFSET          0x302044UL
#define REG_VOL_PB           0x302080UL
#define REG_VOL_SOUND        0x302084UL
#define REG_VSIZE            0x302048UL
#define REG_VSYNC0           0x30204cUL
#define REG_VSYNC1           0x302050UL

#if 0
#define REG_BUSYBITS         0x3020e8UL /* only listed as "reserved" in datasheet */
#define REG_CRC              0x302178UL /* only listed as "reserved" in datasheet */
#define REG_SPI_EARLY_TX     0x30217cUL /* only listed as "reserved" in datasheet */
#define REG_ROMSUB_SEL       0x3020f0UL /* only listed as "reserved" in datasheet */
#define REG_TOUCH_FAULT      0x302170UL /* only listed as "reserved" in datasheet */
#endif


/* FT81x graphics engine specific macros useful for static display list generation */
#define BITMAP_LAYOUT_H(linestride,height) ((40UL<<24)|(((linestride)&3UL)<<2)|(((height)&3UL)<<0))
#define BITMAP_SIZE_H(width,height) ((41UL<<24)|(((width)&3UL)<<2)|(((height)&3UL)<<0))
#define BITMAP_SOURCE(addr) ((1UL<<24)|(((addr)&4194303UL)<<0))
#define NOP() ((45UL<<24))
#define PALETTE_SOURCE(addr) ((42UL<<24)|(((addr)&4194303UL)<<0))
#define SCISSOR_SIZE(width,height) ((28UL<<24)|(((width)&4095UL)<<12)|(((height)&4095UL)<<0))
#define SCISSOR_XY(x,y) ((27UL<<24)|(((x)&2047UL)<<11)|(((y)&2047UL)<<0))
#define VERTEX_FORMAT(frac) ((39UL<<24)|(((frac)&7UL)<<0))
#define VERTEX_TRANSLATE_X(x) ((43UL<<24)|(((x)&131071UL)<<0))
#define VERTEX_TRANSLATE_Y(y) ((44UL<<24)|(((y)&131071UL)<<0))

#endif


/* The following predifined configurations are available
   -----------------------------------------------------
    #define FT8_VM800B35A
    #define FT8_VM800B43A
    #define FT8_VM800B50A
    #define FT8_VM810C
    #define FT8_ME812A
    #define FT8_ME813A
    #define FT8_FT810CB_HY50HD
    #define FT8_FT811CB_HY50HD
    #define FT8_ET07
    #define FT8_RVT70AQ
    #define FT8_EVE2_29
    #define FT8_EVE2_35
    #define FT8_EVE2_35G
    #define FT8_EVE2_38
    #define FT8_EVE2_38G
    #define FT8_EVE2_43
    #define FT8_EVE2_43G
    #define FT8_EVE2_50
    #define FT8_EVE2_50G
    #define FT8_EVE2_70
    #define FT8_EVE2_70G
    #define FT8_NHD_35
    #define FT8_NHD_43
    #define FT8_NHD_50
    #define FT8_NHD_70
    #define FT8_ADAM101
*/


/*
 * =================================
 *  display timing parameters below
 * =================================
*/

/* VM800B35A: FT800 320x240 3.5" FTDI */
#if defined (CONFIG_FT8_VM800B35A)
#define FT8_HSIZE   (320L)  /* Thd Length of visible part of line (in PCLKs) - display width */
#define FT8_VSIZE   (240L)  /* Tvd Number of visible lines (in lines) - display height */

#define FT8_VSYNC0  (0L)    /* Tvf Vertical Front Porch */
#define FT8_VSYNC1  (2L)    /* Tvf + Tvp Vertical Front Porch plus Vsync Pulse width */
#define FT8_VOFFSET (13L)   /* Tvf + Tvp + Tvb Number of non-visible lines (in lines) */
#define FT8_VCYCLE  (263L)  /* Tv Total number of lines (visible and non-visible) (in lines) */
#define FT8_HSYNC0  (0L)    /* Thf Horizontal Front Porch */
#define FT8_HSYNC1  (10L)   /* Thf + Thp Horizontal Front Porch plus Hsync Pulse width */
#define FT8_HOFFSET (70L)   /* Thf + Thp + Thb Length of non-visible part of line (in PCLK cycles) */
#define FT8_HCYCLE  (408L)  /* Th Total length of line (visible and non-visible) (in PCLKs) */
#define FT8_PCLKPOL (0L)    /* PCLK polarity (0 = rising edge, 1 = falling edge) */
#define FT8_SWIZZLE (2L)    /* Defines the arrangement of the RGB pins of the FT800 */
#define FT8_PCLK    (8L)    /* 48MHz / REG_PCLK = PCLK frequency */
#define FT8_CSPREAD (1L)    /* helps with noise, when set to 1 fewer signals are changed simultaneously, reset-default: 1 */
#define FT8_TOUCH_RZTHRESH (1200L)  /* touch-sensitivity */
#define FT8_HAS_CRYSTAL     /* use external crystal or internal oscillator? */
#endif


/* FTDI/BRT EVE modules VM800B43A and VM800B50A  FT800 480x272 4.3" and 5.0" */
#if defined (CONFIG_FT8_VM800B43A) || defined (FT8_VM800B50A)
#define FT8_HSIZE   (480L)
#define FT8_VSIZE   (272L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (10L)
#define FT8_VOFFSET (12L)
#define FT8_VCYCLE  (292L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (41L)
#define FT8_HOFFSET (43L)
#define FT8_HCYCLE  (548L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (5L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL
#endif


/* untested */
/* FTDI/BRT EVE2 modules VM810C50A-D, ME812A-WH50R and ME813A-WH50C, 800x480 5.0" */
#if defined (CONFIG_FT8_VM810C) || defined (FT8_ME812A) || defined (FT8_ME813A)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (3L)
#define FT8_VOFFSET (32L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (48L)
#define FT8_HOFFSET (88L)
#define FT8_HCYCLE  (928L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (0L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL
#endif


/* FT810CB-HY50HD: FT810 800x480 5.0" HAOYU */
#if defined (CONFIG_FT8_FT810CB_HY50HD)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (2L)
#define FT8_VOFFSET (13L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (20L)
#define FT8_HOFFSET (64L)
#define FT8_HCYCLE  (952L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (0L)
#define FT8_TOUCH_RZTHRESH (2000L)  /* touch-sensitivity */
#define FT8_HAS_CRYSTAL
#endif


/* FT811CB-HY50HD: FT811 800x480 5.0" HAOYU */
#if defined (CONFIG_FT8_FT811CB_HY50HD)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (2L)
#define FT8_VOFFSET (13L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (20L)
#define FT8_HOFFSET (64L)
#define FT8_HCYCLE  (952L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)  /* touch-sensitivity */
#define FT8_HAS_CRYSTAL
#endif


/* untested */
/* G-ET0700G0DM6 800x480 7.0" Glyn */
#if defined (CONFIG_FT8_ET07)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (2L)
#define FT8_VOFFSET (35L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (128L)
#define FT8_HOFFSET (203L)
#define FT8_HCYCLE  (1056L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL 0   /* no idea if these come with a crystal populated or not */
#endif


/* RVT70AQxxxxxx 800x480 7.0" Riverdi, various options, FT812/FT813, tested with RVT70UQFNWC0x */
#if defined (CONFIG_FT8_RVT70AQ)
#define FT8_HSIZE   (800L)  /* Thd Length of visible part of line (in PCLKs) - display width */
#define FT8_VSIZE   (480L)  /* Tvd Number of visible lines (in lines) - display height */

#define FT8_VSYNC0  (0L)    /* Tvf Vertical Front Porch */
#define FT8_VSYNC1  (10L)   /* Tvf + Tvp Vertical Front Porch plus Vsync Pulse width */
#define FT8_VOFFSET (23L)   /* Tvf + Tvp + Tvb Number of non-visible lines (in lines) */
#define FT8_VCYCLE  (525L)  /* Tv Total number of lines (visible and non-visible) (in lines) */
#define FT8_HSYNC0  (0L)    /* Thf Horizontal Front Porch */
#define FT8_HSYNC1  (10L)   /* Thf + Thp Horizontal Front Porch plus Hsync Pulse width */
#define FT8_HOFFSET (46L)   /* Thf + Thp + Thb Length of non-visible part of line (in PCLK cycles) */
#define FT8_HCYCLE  (1056L) /* Th Total length of line (visible and non-visible) (in PCLKs) */
#define FT8_PCLKPOL (1L)    /* PCLK polarity (0 = rising edge, 1 = falling edge) */
#define FT8_SWIZZLE (0L)    /* Defines the arrangement of the RGB pins of the FT800 */
#define FT8_PCLK    (2L)    /* 60MHz / REG_PCLK = PCLK frequency 30 MHz */
#define FT8_CSPREAD (1L)    /* helps with noise, when set to 1 fewer signals are changed simultaneously, reset-default: 1 */
#define FT8_TOUCH_RZTHRESH (1800L)  /* touch-sensitivity */
#endif


/* untested */
/* EVE2-29A 320x102 2.9" 1U Matrix Orbital, non-touch, FT812 */
#if defined (CONFIG_FT8_EVE2_29)
#define FT8_HSIZE   (320L)
#define FT8_VSIZE   (102L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (2L)
#define FT8_VOFFSET (156L)
#define FT8_VCYCLE  (262L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (10L)
#define FT8_HOFFSET (70L)
#define FT8_HCYCLE  (408L)
#define FT8_PCLKPOL (0L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (8L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#endif


/* EVE2-35A 320x240 3.5" Matrix Orbital, resistive, or non-touch, FT812 */
#if defined (CONFIG_FT8_EVE2_35)
#define FT8_HSIZE   (320L)
#define FT8_VSIZE   (240L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (2L)
#define FT8_VOFFSET (18L)
#define FT8_VCYCLE  (262L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (10L)
#define FT8_HOFFSET (70L)
#define FT8_HCYCLE  (408L)
#define FT8_PCLKPOL (0L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (8L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#endif


/* EVE2-35G 320x240 3.5" Matrix Orbital, capacitive touch, FT813 */
#if defined (CONFIG_FT8_EVE2_35G)
#define FT8_HSIZE   (320L)
#define FT8_VSIZE   (240L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (2L)
#define FT8_VOFFSET (18L)
#define FT8_VCYCLE  (262L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (10L)
#define FT8_HOFFSET (70L)
#define FT8_HCYCLE  (408L)
#define FT8_PCLKPOL (0L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (8L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_GT911   /* special treatment required for out-of-spec touch-controller */
#endif


/* EVE2-38A 480x116 3.8" 1U Matrix Orbital, resistive touch, FT812 */
#if defined (CONFIG_FT8_EVE2_38)
#define FT8_HSIZE   (480L)
#define FT8_VSIZE   (272L)

#define FT8_VSYNC0  (152L)
#define FT8_VSYNC1  (10L)
#define FT8_VOFFSET (12L)
#define FT8_VCYCLE  (292L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (41L)
#define FT8_HOFFSET (43L)
#define FT8_HCYCLE  (548L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (5L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#endif


/* EVE2-38G 480x116 3.8" 1U Matrix Orbital, capacitive touch, FT813 */
#if defined (CONFIG_FT8_EVE2_38G)
#define FT8_HSIZE   (480L)
#define FT8_VSIZE   (272L)

#define FT8_VSYNC0  (152L)
#define FT8_VSYNC1  (10L)
#define FT8_VOFFSET (12L)
#define FT8_VCYCLE  (292L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (41L)
#define FT8_HOFFSET (43L)
#define FT8_HCYCLE  (548L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (5L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_GT911   /* special treatment required for out-of-spec touch-controller */
#endif


/* untested */
/* EVE2-43A 480x272 4.3" Matrix Orbital, resistive or no touch, FT812 */
#if defined (CONFIG_FT8_EVE2_43)
#define FT8_HSIZE   (480L)
#define FT8_VSIZE   (272L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (10L)
#define FT8_VOFFSET (12L)
#define FT8_VCYCLE  (292L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (41L)
#define FT8_HOFFSET (43L)
#define FT8_HCYCLE  (548L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (5L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#endif


/* EVE2-43G 480x272 4.3" Matrix Orbital, capacitive touch, FT813 */
#if defined (CONFIG_FT8_EVE2_43G)
#define FT8_HSIZE   (480L)
#define FT8_VSIZE   (272L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (10L)
#define FT8_VOFFSET (12L)
#define FT8_VCYCLE  (292L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (41L)
#define FT8_HOFFSET (43L)
#define FT8_HCYCLE  (548L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (5L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_GT911   /* special treatment required for out-of-spec touch-controller */
#endif


/* untested */
/* Matrix Orbital EVE2 modules EVE2-50A, EVE2-70A : 800x480 5.0" and 7.0" resistive, or no touch, FT812 */
#if defined (CONFIG_FT8_EVE2_50) || defined (FT8_EVE2_70)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (3L)
#define FT8_VOFFSET (32L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (48L)
#define FT8_HOFFSET (88L)
#define FT8_HCYCLE  (928L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (0L)
#define FT8_TOUCH_RZTHRESH (1200L)
#endif


/* Matrix Orbital EVE2 modules EVE2-50G, EVE2-70G : 800x480 5.0" and 7.0" capacitive touch, FT813 */
#if defined (CONFIG_FT8_EVE2_50G) || defined (FT8_EVE2_70G)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (3L)
#define FT8_VOFFSET (32L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (48L)
#define FT8_HOFFSET (88L)
#define FT8_HCYCLE  (928L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (0L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_GT911   /* special treatment required for out-of-spec touch-controller */
#endif


/* NHD-3.5-320240FT-CxXx-xxx 320x240 3.5" Newhaven, resistive or capacitive, FT81x */
#if defined (CONFIG_FT8_NHD_35)
#define FT8_HSIZE   (320L)
#define FT8_VSIZE   (240L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (2L)
#define FT8_VOFFSET (13L)
#define FT8_VCYCLE  (263L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (10L)
#define FT8_HOFFSET (70L)
#define FT8_HCYCLE  (408L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (2L)
#define FT8_PCLK    (6L)
#define FT8_CSPREAD (0L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL
#endif


/* untested */
/* NHD-4.3-480272FT-CxXx-xxx 480x272 4.3" Newhaven, resistive or capacitive, FT81x */
#if defined (CONFIG_FT8_NHD_43)
#define FT8_HSIZE   (480L)
#define FT8_VSIZE   (272L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (10L)
#define FT8_VOFFSET (12L)
#define FT8_VCYCLE  (292L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (41L)
#define FT8_HOFFSET (43L)
#define FT8_HCYCLE  (548L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (5L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL
#endif


/* untested */
/* NHD-5.0-800480FT-CxXx-xxx 800x480 5.0" Newhaven, resistive or capacitive, FT81x */
#if defined (CONFIG_FT8_NHD_50)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (3L)
#define FT8_VOFFSET (32L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (48L)
#define FT8_HOFFSET (88L)
#define FT8_HCYCLE  (928L)
#define FT8_PCLKPOL (0L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL
#endif


/* untested */
/* NHD-7.0-800480FT-CxXx-xxx 800x480 7.0" Newhaven, resistive or capacitive, FT81x */
#if defined (CONFIG_FT8_NHD_70)
#define FT8_HSIZE   (800L)
#define FT8_VSIZE   (480L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (3L)
#define FT8_VOFFSET (32L)
#define FT8_VCYCLE  (525L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (48L)
#define FT8_HOFFSET (88L)
#define FT8_HCYCLE  (928L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL
#endif


/* ADAM101-LCP-SWVGA-NEW 1024x600 10.1" Glyn, capacitive, FT813 */
#if defined (CONFIG_FT8_ADAM101)
#define FT8_HSIZE   (1024L)
#define FT8_VSIZE   (600L)

#define FT8_VSYNC0  (0L)
#define FT8_VSYNC1  (1L)
#define FT8_VOFFSET (1L)
#define FT8_VCYCLE  (720L)
#define FT8_HSYNC0  (0L)
#define FT8_HSYNC1  (1L)
#define FT8_HOFFSET (1L)
#define FT8_HCYCLE  (1100L)
#define FT8_PCLKPOL (1L)
#define FT8_SWIZZLE (0L)
#define FT8_PCLK    (2L)
#define FT8_CSPREAD (1L)
#define FT8_TOUCH_RZTHRESH (1200L)
#define FT8_HAS_CRYSTAL
#endif


#endif //CONFIG_MICROPY_USE_EVE

#endif /* _FT8_H_ */
