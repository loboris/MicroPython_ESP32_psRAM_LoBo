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
 *  Author: LoBo (loboris@gmail.com, loboris.github)
 *
 *  Module supporting SPI TFT displays based on ILI9341 & ILI9488 controllers
 * 
 * HIGH SPEED LOW LEVEL DISPLAY FUNCTIONS
 * USING DIRECT or DMA SPI TRANSFER MODEs
 *
*/

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_TFT

#include <string.h>
#include "tftspi.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "soc/spi_reg.h"
#include "esp_log.h"


// ====================================================
// ==== Global variables, default values ==============

// Converts colors to grayscale if set to 1
uint8_t gray_scale = 0;
// Spi clock for reading data from display memory in Hz
uint32_t max_rdclock = 4000000;

// Default display dimensions
int _width = DEFAULT_TFT_DISPLAY_WIDTH;
int _height = DEFAULT_TFT_DISPLAY_HEIGHT;

// Display type, DISP_TYPE_ILI9488 or DISP_TYPE_ILI9341
uint8_t tft_disp_type = DEFAULT_DISP_TYPE;
uint8_t tft_touch_type = TOUCH_TYPE_NONE;

// Spi device handles for display and touch screen
exspi_device_handle_t *disp_spi = NULL;
exspi_device_handle_t *ts_spi = NULL;

uint8_t bits_per_color = 16;
uint8_t TFT_RGB_BGR = 0;
uint8_t gamma_curve = 0;
uint32_t spi_speed = 10000000;
// ====================================================

static color_t *trans_cline = NULL;
static const char TAG[] = "[TFTSPI]";
static uint8_t invertrot = 0;

uint8_t spibus_is_init = 0;

// RGB to GRAYSCALE constants
// 0.2989  0.5870  0.1140
#define GS_FACT_R 0.2989
#define GS_FACT_G 0.4870
#define GS_FACT_B 0.2140

#ifdef CONFIG_MICROPY_USE_EPD

static uint16_t xDot = 128;
static uint16_t yDot = 296;

//static uint8_t GDOControl[] = {0x01, (_width-1)%256, (_width-1)/256, 0x00};
static uint8_t softstart[4] = {0x0c, 0xd7, 0xd6, 0x9d};
static uint8_t VCOMVol[2] = {0x2c, 0xa8};           // VCOM 7c
static uint8_t DummyLine[2] = {0x3a, 0x1a};         // 4 dummy line per gate
static uint8_t Gatetime[2] = {0x3b, 0x08};          // 2us per line
static uint8_t RamDataEntryMode[2] = {0x11, 0x01};  // Ram data entry mode
static uint8_t Border[2] = {0x3c, 0x61};            // Border control ( 0x61: white border; 0x51: black border

/*
There are totally 20 phases for programmable Source waveform of different phase length.
The phase period defined as TP [n] * T FRAME , where TP [n] range from 0 to 15.
TP [n] = 0 indicates phase skipped
Source Voltage Level: VS [n-XY] is constant in each phase
VS [n-XY] indicates the voltage in phase n for transition from GS X to GS Y
 00 – VSS
 01 – VSH
 10 – VSL
 11 – NA
VS [n-XY] and TP[n] are stored in waveform lookup table register [LUT].

VS coding: VS[0-11] VS[0-10] VS[0-01] VS[0-00]

*/
//                                   ---                                             VS                                             ----  ----           TP                            ----
//uint8_t LUTDefault_full[31] = {0x32, 0x02,0x02,0x01,0x11,0x12,0x12,0x22,0x22,0x66,0x69,0x69,0x59,0x58,0x99,0x99,0x88,0x00,0x00,0x00,0x00, 0xF8,0xB4,0x13,0x51,0x35,0x51,0x51,0x19,0x01,0x00};
uint8_t LUTDefault_full[31] = {0x32, 0x11,0x11,0x10,0x02,0x02,0x22,0x22,0x22,0x22,0x22,0x51,0x51,0x55,0x88,0x08,0x08,0x88,0x88,0x00,0x00, 0x34,0x23,0x12,0x21,0x24,0x28,0x22,0x21,0xA1,0x01};
uint8_t LUTDefault_part[31] = {0x32, 0x10,0x18,0x18,0x08,0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x13,0x14,0x44,0x12,0x00,0x00,0x00,0x00,0x00,0x00};
uint8_t LUT_gs[31]          = {0x32, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
uint8_t LUTFastest[31]      = {0x32, 0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

uint8_t lvl_buf[16] = {32,70,110,150,185,210,220,225,230,235,240,243,248,251,253,255};
uint8_t lvl_buf_jpg[16] = {4,8,12,16,22,30,40,60,80,110,140,180,220,240,250,255};

#endif

// ==== Functions =====================

//---------------------
esp_err_t disp_select()
{
	//wait_trans_finish(1);
	return spi_device_select(disp_spi, 0);
}

//-----------------------
esp_err_t disp_deselect()
{
	//wait_trans_finish(1);
	return spi_device_deselect(disp_spi);
}

// Send command with data to display, display must be selected
//----------------------------------------------------------------------
void disp_spi_transfer_cmd_data(int8_t cmd, uint8_t *data, uint32_t len)
{
	while (disp_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

    // Set DC to 0 (command mode);
    gpio_set_level(disp_spi->dc, 0);

    disp_spi->handle->host->hw->data_buf[0] = (uint32_t)cmd;
    _spi_transfer_start(disp_spi, 8, 0);

	if ((len == 0) || (data == NULL)) return;

	while (disp_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    // Set DC to 1 (data mode);
	gpio_set_level(disp_spi->dc, 1);

	uint8_t idx=0, bidx=0;
	uint32_t bits=0;
	uint32_t count=0;
	uint32_t wd = 0;
	while (count < len) {
		// get data byte from buffer, fill spi buffer
		wd |= (uint32_t)data[count] << bidx;
    	count++;
    	bits += 8;
		bidx += 8;
    	if (count == len) {
    		disp_spi->handle->host->hw->data_buf[idx] = wd;
    		break;
    	}
		if (bidx == 32) {
			disp_spi->handle->host->hw->data_buf[idx] = wd;
			idx++;
			bidx = 0;
			wd = 0;
		}
    	if (idx == 16) {
    		// SPI buffer full, send data
			_spi_transfer_start(disp_spi, bits, 0);
    		
			bits = 0;
    		idx = 0;
			bidx = 0;
    	}
    }
    if (bits > 0) _spi_transfer_start(disp_spi, bits, 0);
}

// Send 1 byte display command, display must be selected
//------------------------------------
void disp_spi_transfer_cmd(int8_t cmd)
{
	while (disp_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

	// Set DC to 0 (command mode);
    gpio_set_level(disp_spi->dc, 0);

    disp_spi->handle->host->hw->data_buf[0] = (uint32_t)cmd;
    _spi_transfer_start(disp_spi, 8, 0);
}

// Set the address window for display write & read commands, display must be selected
//-----------------------------------------------------------------------------------------
static void disp_spi_transfer_addrwin(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2) {
	uint32_t wd;

	disp_spi_transfer_cmd(TFT_CASET);

	wd = (uint32_t)(x1 >> 8);
	wd |= (uint32_t)(x1 & 0xff) << 8;
	wd |= (uint32_t)(x2 >> 8) << 16;
	wd |= (uint32_t)(x2 & 0xff) << 24;

	while (disp_spi->handle->host->hw->cmd.usr); // wait transfer end
	gpio_set_level(disp_spi->dc, 1);
	disp_spi->handle->host->hw->data_buf[0] = wd;
    _spi_transfer_start(disp_spi, 32, 0);

	disp_spi_transfer_cmd(TFT_PASET);

	wd = (uint32_t)(y1 >> 8);
	wd |= (uint32_t)(y1 & 0xff) << 8;
	wd |= (uint32_t)(y2 >> 8) << 16;
	wd |= (uint32_t)(y2 & 0xff) << 24;

	while (disp_spi->handle->host->hw->cmd.usr); // wait transfer end
	gpio_set_level(disp_spi->dc, 1);
	disp_spi->handle->host->hw->data_buf[0] = wd;
    _spi_transfer_start(disp_spi, 32, 0);
}

// Convert color to gray scale
//------------------------------------
static color_t color2gs(color_t color)
{
	color_t _color;
    float gs_clr = GS_FACT_R * color.r + GS_FACT_G * color.g + GS_FACT_B * color.b;
    if (gs_clr > 255) gs_clr = 255;

    _color.r = (uint8_t)gs_clr;
    _color.g = (uint8_t)gs_clr;
    _color.b = (uint8_t)gs_clr;

    return _color;
}

// Convert color to 16-bit 565 RGB value
//------------------------------------
static uint16_t color16(color_t color)
{
	uint16_t _color;
	_color = (uint16_t)(color.r & 0xF8) << 8;
	_color |= (uint16_t)(color.g & 0xFC) << 3;
	_color |= (uint16_t)(color.b & 0xF8) >> 3;
    return _color;
}

//============================================
esp_err_t wait_trans_finish(uint8_t free_line)
{
	_wait_trans_finish(disp_spi);

	if ((free_line) && (trans_cline)) {
		free(trans_cline);
		trans_cline = NULL;
	}
    return ESP_OK;
}

// Set display pixel at given coordinates to given color
//==============================================================
void drawPixel(int16_t x, int16_t y, color_t color, uint8_t sel)
{
	if ((sel) && (disp_select() != ESP_OK)) return;

	uint32_t wd = 0;
    color_t _color = color;
	if (gray_scale) _color = color2gs(color);

	wait_trans_finish(1);
	disp_spi_transfer_addrwin(x, x+1, y, y+1);

	// Send RAM WRITE command
	disp_spi_transfer_cmd(TFT_RAMWR);

	if (bits_per_color == 16) {
		uint16_t _color16 = color16(_color);
		wd = (uint32_t)(_color16 >> 8);
		wd |= (uint32_t)(_color16 & 0xFF) << 8;
	}
	else {
		wd = (uint32_t)_color.r;
		wd |= (uint32_t)_color.g << 8;
		wd |= (uint32_t)_color.b << 16;
	}
	while (disp_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    // Set DC to 1 (data mode);
	gpio_set_level(disp_spi->dc, 1);
	disp_spi->handle->host->hw->data_buf[0] = wd;
    _spi_transfer_start(disp_spi, bits_per_color, 0);

	if (sel) disp_deselect();
}

// Send colors from color buffer directly, maximum of 512 bits
//-----------------------------------------------------------------
static void _direct_send(color_t *color, uint32_t len, uint8_t rep)
{
	uint32_t cidx = 0;	// color buffer index
	uint32_t wd = 0;
	uint16_t _color16 = 0;
	int idx = 0;
	int bits = 0;
	int wbits = 0;

	// Get first color data from color buffer (repeat color)
	color_t _color = color[0];
	if ((rep) && (gray_scale)) _color = color2gs(color[0]);
	if (bits_per_color == 16) _color16 = color16(_color);

	while (len) {
		if (rep == 0) {
			// Get color data from color buffer
			if (gray_scale) _color = color2gs(color[cidx]);
			else _color = color[cidx];
			if (bits_per_color == 16) _color16 = color16(_color);
		}

		if (bits_per_color == 16) {
			wd |= (uint32_t)(_color16 >> 8) << wbits;
			wbits += 8;
			wd |= (uint32_t)(_color16 & 0xFF) << wbits;
			wbits += 8;
			if (wbits == 32) {
				bits += wbits;
				wbits = 0;
				disp_spi->handle->host->hw->data_buf[idx++] = wd;
				wd = 0;
			}
		}
		else {
			wd |= (uint32_t)_color.r << wbits;
			wbits += 8;
			if (wbits == 32) {
				bits += wbits;
				wbits = 0;
				disp_spi->handle->host->hw->data_buf[idx++] = wd;
				wd = 0;
			}
			wd |= (uint32_t)_color.g << wbits;
			wbits += 8;
			if (wbits == 32) {
				bits += wbits;
				wbits = 0;
				disp_spi->handle->host->hw->data_buf[idx++] = wd;
				wd = 0;
			}
			wd |= (uint32_t)_color.b << wbits;
			wbits += 8;
			if (wbits == 32) {
				bits += wbits;
				wbits = 0;
				disp_spi->handle->host->hw->data_buf[idx++] = wd;
				wd = 0;
			}
		}
    	len--;	// Decrement colors counter
        cidx++;	// increment color buffer index (used only if not repeating color)
    }
	if (bits) _spi_transfer_start(disp_spi, bits, 0);
}

// ==================================================================
// === Main function to send data to display ========================
// If  rep==true:  repeat sending color data to display 'len' times
// If rep==false:  send 'len' color data from color buffer to display
// ** Device must already be selected and address window set **
// ==================================================================
//------------------------------------------------------------------------------------
static void _TFT_pushColorRep(color_t *color, uint32_t len, uint8_t rep, uint8_t wait)
{
	if (len == 0) return;

	// Send RAM WRITE command
	disp_spi_transfer_cmd(TFT_RAMWR);
	while (disp_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

	gpio_set_level(disp_spi->dc, 1); // Set DC to 1 (data mode);

	if ((len * bits_per_color) <= 512) {
		// --- up to 512 bits, send directly ---
		_direct_send(color, len, rep);
	}
	else if (rep == 0)  {
		// --- more than 512 bits, no repeat: use DMA transfer ---
		if (trans_cline) return;
		int buflen = ((len * bits_per_color) + 31) / 8;
		trans_cline = heap_caps_malloc(buflen, MALLOC_CAP_DMA);
		if (trans_cline == NULL) return;

		// ** Prepare data
		if ((!gray_scale) && (bits_per_color != 16)) {
			memcpy(trans_cline, color, len*(bits_per_color/8));
		}
		else {
			color_t _color;
			uint16_t _color16;
			uint8_t *trans_cline16 = (uint8_t *)trans_cline;
			for (int n=0; n<len; n++) {
				if (gray_scale) _color = color2gs(color[n]);
				else _color = color[n];
				if (bits_per_color == 16) {
					_color16 = color16(_color);
					trans_cline16[n*2] = (uint8_t)(_color16 >> 8);
					trans_cline16[(n*2)+1] = (uint8_t)(_color16 & 0xFF);
				}
				else trans_cline[n] = _color;
			}
		}
	    _dma_send(disp_spi, (uint8_t *)trans_cline, len * (bits_per_color/8));
	}
	else {
		// --- more than 512 bits, repeat color ---
		if (trans_cline) return;
		color_t _color;
		uint16_t  _color16 = 0;
		uint8_t *trans_cline16 = NULL;
		uint32_t buf_colors;
		int buf_bytes, to_send;

		// Prepare color buffer of maximum 2 lines
		buf_colors = ((len > (_width*2)) ? (_width*2) : len);
		buf_bytes = buf_colors * (bits_per_color / 8);
		int buflen = ((buf_bytes*8) + 31) / 8;

		trans_cline = heap_caps_malloc(buflen, MALLOC_CAP_DMA);
		if (trans_cline == NULL) return;

		// Prepare fill color
		if (gray_scale) _color = color2gs(color[0]);
		else _color = color[0];
		if (bits_per_color == 16) {
			_color16 = color16(_color);
			trans_cline16 = (uint8_t *)trans_cline;
		}
		// Fill color buffer with fill color
		for (uint32_t i=0; i<buf_colors; i++) {
			if (bits_per_color == 16) {
				trans_cline16[i*2] = (uint8_t)(_color16 >> 8);
				trans_cline16[(i*2)+1] = (uint8_t)(_color16 & 0xFF);
			}
			else trans_cline[i] = _color;
		}

		// Send 'len' colors
		to_send = len;
		while (to_send > 0) {
			wait_trans_finish(0);
			_dma_send(disp_spi, (uint8_t *)trans_cline, ((to_send > buf_colors) ? buf_bytes : (to_send * (bits_per_color/8))));
			to_send -= buf_colors;
		}
	}

	if (wait) wait_trans_finish(1);
}

// Write 'len' color data to TFT 'window' (x1,y2),(x2,y2)
//================================================================================
void TFT_pushColorRep(int x1, int y1, int x2, int y2, color_t color, uint32_t len)
{
	wait_trans_finish(1);
	if (disp_select() != ESP_OK) return;

	// ** Send address window **
	disp_spi_transfer_addrwin(x1, x2, y1, y2);

	// Send repeated color and wait for all data sent
	_TFT_pushColorRep(&color, len, 1, 1);

	disp_deselect();
}

// Write 'len' color data to TFT 'window' (x1,y2),(x2,y2) from given buffer
// === Device must already be selected ===
//======================================================================================
void send_data(int x1, int y1, int x2, int y2, uint32_t len, color_t *buf, uint8_t wait)
{
	wait_trans_finish(1);
	// ** Send address window **
	disp_spi_transfer_addrwin(x1, x2, y1, y2);

	// Send color buffer
	_TFT_pushColorRep(buf, len, 0, wait);
}

//=========================================
uint32_t read_cmd(uint8_t cmd, uint8_t len)
{
	if (disp_select() != ESP_OK) return -2;

	// Send command command
	disp_spi_transfer_cmd(cmd);

	while (disp_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

	// Read data;
	gpio_set_level(disp_spi->dc, 1);
    disp_spi->handle->host->hw->data_buf[0] = 0xFFFFFFFF;

    _spi_transfer_start(disp_spi, 0, len*8);
	while (disp_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

	uint32_t res = disp_spi->handle->host->hw->data_buf[0];

	disp_deselect();
	return res;
}

// Reads 'len' pixels/colors from the TFT's GRAM 'window'
// 'buf' is an array of bytes with 1st byte reserved for reading 1 dummy byte
// and the rest is actually an array of color_t values
//==================================================================================
int read_data(int x1, int y1, int x2, int y2, int len, uint8_t *buf, uint8_t set_sp)
{
	spi_transaction_t t;
	uint32_t current_clock = 0;

    memset(&t, 0, sizeof(t));  //Zero out the transaction
	memset(buf, 0, len*sizeof(color_t));

	if (set_sp) {
		if (disp_deselect() != ESP_OK) return -1;
		// Change spi clock if needed
		current_clock = spi_get_speed(disp_spi);
		if (max_rdclock < current_clock) spi_set_speed(disp_spi, max_rdclock);
	}

	if (disp_select() != ESP_OK) return -2;

	// ** Send address window **
	disp_spi_transfer_addrwin(x1, x2, y1, y2);

    // ** GET pixels/colors **
	disp_spi_transfer_cmd(TFT_RAMRD);

    t.length = 0;					//Send nothing
    t.tx_buffer = NULL;
    t.rxlength = 8 * ((len*3)+1);	//Receive size in bits
    t.rx_buffer = buf;

	esp_err_t res = spi_transfer_data_nodma(disp_spi, &t); // Receive using direct mode

	disp_deselect();

	if (set_sp) {
		// Restore spi clock if needed
		if (max_rdclock < current_clock) spi_set_speed(disp_spi, current_clock);
	}

    return res;
}

// Reads one pixel/color from the TFT's GRAM at position (x,y)
//=====================================
color_t readPixel(int16_t x, int16_t y)
{
    uint8_t color_buf[sizeof(color_t)+1] = {0};

    read_data(x, y, x+1, y+1, 1, color_buf, 1);

    color_t color;
	color.r = color_buf[1];
	color.g = color_buf[2];
	color.b = color_buf[3];
	return color;
}

// get 16-bit data from touch controller for specified type
// ** Touch device must already be selected **
//==============================
int touch_get_data(uint8_t type)
{
	/*
	while (ts_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

    ts_spi->handle->host->hw->data_buf[0] = type;
    _spi_transfer_start(ts_spi, 24, 24);
    uint16_t res = (uint16_t)(ts_spi->handle->host->hw->data_buf[0] >> 8);
	while (ts_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
	*/
	spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    uint32_t buf = type;
    t.length = 24;
    t.tx_buffer = (uint8_t *)&buf;
    t.rxlength = 24;
    t.rx_buffer = (uint8_t *)&buf;
	esp_err_t ret = spi_transfer_data_nodma(ts_spi, &t);
	if (ret != ESP_OK) return 0;

    uint16_t res = (uint16_t)(buf >> 8);

    return res;
}

// ==== STMPE610 ===============================================================


// ----- STMPE610 --------------------------------------------------------------------------

//------------------------------------------------------
static void stmpe610_write_reg(uint8_t reg, uint8_t val)
{
	spi_device_select(ts_spi, 0);
    ts_spi->handle->host->hw->data_buf[0] = (val << 8) | reg;
    _spi_transfer_start(ts_spi, 16, 16);
	while (ts_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    spi_device_deselect(ts_spi);
}

//--------------------------------------------
static uint8_t stmpe610_read_byte(uint8_t reg)
{
	spi_device_select(ts_spi, 0);
    ts_spi->handle->host->hw->data_buf[0] = (reg << 8) | (reg | 0x80);
    _spi_transfer_start(ts_spi, 24, 24);
	while (ts_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    uint8_t res = (uint8_t)(ts_spi->handle->host->hw->data_buf[0] >> 8);
    spi_device_deselect(ts_spi);
    return res;
}

//---------------------------------------------
static uint16_t stmpe610_read_word(uint8_t reg)
{
	spi_device_select(ts_spi, 0);
    ts_spi->handle->host->hw->data_buf[0] = ((((reg+1) << 8) | ((reg+1) | 0x80)) << 16) | (reg << 8) | (reg | 0x80);
    _spi_transfer_start(ts_spi, 32, 32);
	while (ts_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    uint16_t res = (uint16_t)(ts_spi->handle->host->hw->data_buf[0] & 0xFF00);
    res |= (uint16_t)(ts_spi->handle->host->hw->data_buf[0] >> 24);
    spi_device_deselect(ts_spi);

    return res;
}

//-----------------------
uint32_t stmpe610_getID()
{
    uint16_t tid = stmpe610_read_word(0);
    uint8_t tver = stmpe610_read_byte(2);
    return (tid << 8) | tver;
}

//==================
void stmpe610_Init()
{
	/*
    stmpe610_write_reg(STMPE610_REG_SYS_CTRL1, 0x02);        // Software chip reset
    vTaskDelay(10 / portTICK_RATE_MS);

    stmpe610_write_reg(STMPE610_REG_SYS_CTRL2, 0x04);        // Temperature sensor clock off, GPIO clock off, touch clock on, ADC clock on

    stmpe610_write_reg(STMPE610_REG_INT_EN, 0x00);           // Don't Interrupt on INT pin

    stmpe610_write_reg(STMPE610_REG_ADC_CTRL1, 0x48);        // ADC conversion time = 80 clock ticks, 12-bit ADC, internal voltage reference
    vTaskDelay(2 / portTICK_RATE_MS);
    stmpe610_write_reg(STMPE610_REG_ADC_CTRL2, 0x01);        // ADC speed 3.25MHz
    stmpe610_write_reg(STMPE610_REG_GPIO_AF, 0x00);          // GPIO alternate function - OFF
    stmpe610_write_reg(STMPE610_REG_TSC_CFG, 0xE3);          // Averaging 8, touch detect delay 1ms, panel driver settling time 1ms
    stmpe610_write_reg(STMPE610_REG_FIFO_TH, 0x01);          // FIFO threshold = 1
    stmpe610_write_reg(STMPE610_REG_FIFO_STA, 0x01);         // FIFO reset enable
    stmpe610_write_reg(STMPE610_REG_FIFO_STA, 0x00);         // FIFO reset disable
    stmpe610_write_reg(STMPE610_REG_TSC_FRACT_XYZ, 0x07);    // Z axis data format
    stmpe610_write_reg(STMPE610_REG_TSC_I_DRIVE, 0x01);      // max 50mA touchscreen line current
    stmpe610_write_reg(STMPE610_REG_TSC_CTRL, 0x30);         // X&Y&Z, 16 reading window
    stmpe610_write_reg(STMPE610_REG_TSC_CTRL, 0x31);         // X&Y&Z, 16 reading window, TSC enable
    stmpe610_write_reg(STMPE610_REG_INT_STA, 0xFF);          // Clear all interrupts
    stmpe610_write_reg(STMPE610_REG_INT_CTRL, 0x00);         // Level interrupt, disable interrupts
    */

	stmpe610_write_reg(STMPE610_REG_SYS_CTRL1, 0x02);        // Software chip reset
    vTaskDelay(10 / portTICK_RATE_MS);
    stmpe610_write_reg(STMPE610_REG_SYS_CTRL2, 0x0);													// turn on clocks!
    vTaskDelay(2 / portTICK_RATE_MS);
    stmpe610_write_reg(STMPE610_REG_TSC_CTRL, STMPE610_REG_TSC_CTRL_XYZ | STMPE610_REG_TSC_CTRL_EN);	// XYZ and enable!
    stmpe610_write_reg(STMPE610_REG_INT_EN, STMPE610_REG_INT_EN_TOUCHDET);
    stmpe610_write_reg(STMPE610_REG_ADC_CTRL1, STMPE610_REG_ADC_CTRL1_10BIT | (0x6 << 4));				// 96 clocks per conversion
    vTaskDelay(2 / portTICK_RATE_MS);
    stmpe610_write_reg(STMPE610_REG_ADC_CTRL2, STMPE610_REG_ADC_CTRL2_6_5MHZ);
    stmpe610_write_reg(STMPE610_REG_TSC_CFG, STMPE610_REG_TSC_CFG_4SAMPLE | STMPE610_REG_TSC_CFG_DELAY_1MS | STMPE610_REG_TSC_CFG_SETTLE_5MS);
    stmpe610_write_reg(STMPE610_REG_TSC_FRACT_XYZ, 0x6);
    stmpe610_write_reg(STMPE610_REG_FIFO_TH, 1);
    stmpe610_write_reg(STMPE610_REG_FIFO_STA, STMPE610_REG_FIFO_STA_RESET);
    stmpe610_write_reg(STMPE610_REG_FIFO_STA, 0);														// unreset
    stmpe610_write_reg(STMPE610_REG_TSC_I_DRIVE, STMPE610_REG_TSC_I_DRIVE_50MA);
    stmpe610_write_reg(STMPE610_REG_INT_STA, 0xFF);														// reset all ints
    stmpe610_write_reg(STMPE610_REG_INT_CTRL, STMPE610_REG_INT_CTRL_POL_HIGH | STMPE610_REG_INT_CTRL_ENABLE);
}

//---------------------------------------------------------------
static void stmpe_readData(uint32_t *x, uint32_t *y, uint32_t *z)
{
    uint8_t data[4];

    data[0] = stmpe610_read_byte(STMPE610_REG_TSC_DATA);
    data[1] = stmpe610_read_byte(STMPE610_REG_TSC_DATA);
    data[2] = stmpe610_read_byte(STMPE610_REG_TSC_DATA);
    data[3] = stmpe610_read_byte(STMPE610_REG_TSC_DATA);

    *x += ((data[0]) << 4) | (data[1] >> 4);
    *y += ((data[1] & 0x0F) << 8) | data[2];
    *z += data[3];

    if ((stmpe610_read_byte(STMPE610_REG_FIFO_STA) & STMPE610_REG_FIFO_STA_EMPTY)) {
		stmpe610_write_reg(STMPE610_REG_INT_STA, 0xFF);  // reset all interrupts
	}
}

//===========================================================
int stmpe610_get_touch(uint16_t *x, uint16_t *y, uint16_t *z)
{
	/*
    if (!(stmpe610_read_byte(STMPE610_REG_TSC_CTRL) & 0x80)) return 0;

    int n = 0;
    // Get touch data
    uint8_t fifo_size = stmpe610_read_byte(STMPE610_REG_FIFO_SIZE);
    while (fifo_size < 2) {
    	if (!(stmpe610_read_byte(STMPE610_REG_TSC_CTRL) & 0x80)) return 0;
        fifo_size = stmpe610_read_byte(STMPE610_REG_FIFO_SIZE);
        n++;
        if (n > 1000) return 0;
    }
    n = 0;
    while (fifo_size > 120) {
    	if (!(stmpe610_read_byte(STMPE610_REG_TSC_CTRL) & 0x80)) return 0;
        *x = stmpe610_read_word(STMPE610_REG_TSC_DATA_X);
        *y = stmpe610_read_word(STMPE610_REG_TSC_DATA_Y);
        *z = stmpe610_read_byte(STMPE610_REG_TSC_DATA_Z);
        fifo_size = stmpe610_read_byte(STMPE610_REG_FIFO_SIZE);
        n++;
        if (n > 1000) return 0;
    }
    for (uint8_t i=0; i < (fifo_size-1); i++) {
        *x = stmpe610_read_word(STMPE610_REG_TSC_DATA_X);
        *y = stmpe610_read_word(STMPE610_REG_TSC_DATA_Y);
        *z = stmpe610_read_byte(STMPE610_REG_TSC_DATA_Z);
    }

    *x = 4096 - *x;
    // Clear the rest of the fifo
    //{
    //    stmpe610_write_reg(STMPE610_REG_FIFO_STA, 0x01);		// FIFO reset enable
    //    stmpe610_write_reg(STMPE610_REG_FIFO_STA, 0x00);		// FIFO reset disable
    //}
    */

	*x = 0;
	*y = 0;
	*z = 0;
    if (!(stmpe610_read_byte(STMPE610_REG_TSC_CTRL) & 0x80)) {
    	// no touch data
    	return 0;
    }

    int n = 0;
	uint32_t tx = 0;
	uint32_t ty = 0;
	uint32_t tz = 0;
	while (!((stmpe610_read_byte(STMPE610_REG_FIFO_STA) & STMPE610_REG_FIFO_STA_EMPTY))) {
		stmpe_readData(&tx, &ty, &tz);
		n++;
	}

	if (n) {
		// return the average values
		*x = 4096 - (uint16_t)(tx / n);
		*y = (uint16_t)(ty / n);
		*z = (uint16_t)(tz / n);
	}

	return 1;
}

// ==== STMPE610 ===========================================================================


// Find maximum spi clock for successful read from display RAM
// ** Must be used AFTER the display is initialized **
//======================
uint32_t find_rd_speed()
{
	esp_err_t ret;
	color_t color;
	uint32_t max_speed = 1000000;
    uint32_t change_speed, cur_speed;
    int line_check;
    color_t *color_line = NULL;
    uint8_t *line_rdbuf = NULL;
    uint8_t gs = gray_scale;

    gray_scale = 0;
    cur_speed = spi_get_speed(disp_spi);

	color_line = malloc(_width*3);
    if (color_line == NULL) goto exit;

    line_rdbuf = malloc((_width*3)+1);
	if (line_rdbuf == NULL) goto exit;

	color_t *rdline = (color_t *)(line_rdbuf+1);

	// Fill test line with colors
	color = (color_t){0xE8,0xAC,0x78};
	for (int x=0; x<_width; x++) {
		color_line[x] = color;
	}

	// Find maximum read spi clock
	for (uint32_t speed=2000000; speed<=cur_speed; speed += 1000000) {
		change_speed = spi_set_speed(disp_spi, speed);
		if (change_speed == 0) goto exit;

		memset(line_rdbuf, 0, _width*sizeof(color_t)+1);

		if (disp_select()) goto exit;
		// Write color line
		send_data(0, _height/2, _width-1, _height/2, _width, color_line, 1);
		if (disp_deselect()) goto exit;

		// Read color line
		ret = read_data(0, _height/2, _width-1, _height/2, _width, line_rdbuf, 0);

		// Compare
		line_check = 0;
		if (ret == ESP_OK) {
			for (int y=0; y<_width; y++) {
				if ((color_line[y].r & 0xFC) != (rdline[y].r & 0xFC)) line_check = 1;
				else if ((color_line[y].g & 0xFC) != (rdline[y].g & 0xFC)) line_check = 1;
				else if ((color_line[y].b & 0xFC) != (rdline[y].b & 0xFC)) line_check =  1;
				if (line_check) break;
			}
		}
		else line_check = ret;

		if (line_check) break;
		max_speed = speed;
	}

exit:
    gray_scale = gs;
	if (line_rdbuf) free(line_rdbuf);
	if (color_line) free(color_line);

	// restore spi clk
	change_speed = spi_set_speed(disp_spi, cur_speed);

	return max_speed;
}

//---------------------------------------------------------------------------
// Companion code to the initialization table.
// Reads and issues a series of LCD commands stored in byte array
//--------------------------------------------
static void commandList(const uint8_t *addr) {
  uint8_t  numCommands, numArgs, cmd;
  uint16_t ms;

  numCommands = *addr++;				// Number of commands to follow
  while(numCommands--) {				// For each command...
    cmd = *addr++;						// save command
    numArgs  = *addr++;					// Number of args to follow
    ms       = numArgs & TFT_CMD_DELAY;	// If high bit set, delay follows args
    numArgs &= ~TFT_CMD_DELAY;			// Mask out delay bit

	disp_spi_transfer_cmd_data(cmd, (uint8_t *)addr, numArgs);

	addr += numArgs;

    if(ms) {
      ms = *addr++;              // Read post-command delay time (ms)
      if(ms == 255) ms = 500;    // If 255, delay for 500 ms
	  vTaskDelay(ms / portTICK_RATE_MS);
    }
  }
}

//==================================
void _tft_setRotation(uint8_t rot) {
	uint8_t rotation = rot & 3; // can't be higher than 3
	uint8_t send = 1;
	uint8_t madctl = 0;
	uint16_t tmp;

    if ((rotation & 1)) {
        // in landscape modes must be width > height
        if (_width < _height) {
            tmp = _width;
            _width  = _height;
            _height = tmp;
        }
    }
    else {
        // in portrait modes must be width < height
        if (_width > _height) {
            tmp = _width;
            _width  = _height;
            _height = tmp;
        }
    }
    if (invertrot == 2) {
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (MADCTL_MX | TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MY | TFT_RGB_BGR);
            break;
        }
    }
    else if (invertrot == 3) {
        // used for M5Stack display
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MX | MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MY | MADCTL_MX | TFT_RGB_BGR);
            break;
        }
    }
    else if (invertrot == 1) {
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MY | MADCTL_MX | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MX | MADCTL_MV | TFT_RGB_BGR);
            break;
        }
    }
    else {
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MX | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (MADCTL_MV | TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (MADCTL_MY | TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MX | MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
            break;
        }
    }
	if (send) {
		if (disp_select() == ESP_OK) {
			disp_spi_transfer_cmd_data(TFT_MADCTL, &madctl, 1);
			disp_deselect();
		}
	}

}

//---------------------------------------
void bcklOff(display_config_t *dconfig) {
    if (dconfig->bckl >= 0) {
		gpio_set_level(dconfig->bckl, (dconfig->bckl_on & 1) ^ 1);
    }
}

//--------------------------------------
void bcklOn(display_config_t *dconfig) {
    if (dconfig->bckl >= 0) {
		gpio_set_level(dconfig->bckl, dconfig->bckl_on & 1);
    }
}

//===========================================
void _tft_setBitsPerColor(uint8_t bitsperc) {
	if ((tft_disp_type != DISP_TYPE_ILI9488) && ((bitsperc == 16) || (bitsperc == 24))) {
		uint8_t bpc = DISP_COLOR_BITS_16;
		if (bitsperc == 24) bpc = DISP_COLOR_BITS_24;
		if (disp_select() == ESP_OK) {
			disp_spi_transfer_cmd_data(TFT_CMD_PIXFMT, &bpc, 1);
			disp_deselect();
		}
		bits_per_color = bitsperc;
	}
}

//--------------------------------------------------------
static void TFT_PinsInit(display_config_t *dconfig)
{
    // Route all used pins to GPIO control
	if (!spibus_is_init) {
		gpio_pad_select_gpio(dconfig->miso);
		gpio_pad_select_gpio(dconfig->mosi);
		gpio_pad_select_gpio(dconfig->sck);

		gpio_set_direction(dconfig->miso, GPIO_MODE_INPUT);
		gpio_set_pull_mode(dconfig->miso, GPIO_PULLUP_ONLY);
		gpio_set_direction(dconfig->mosi, GPIO_MODE_OUTPUT);
		gpio_set_direction(dconfig->sck, GPIO_MODE_OUTPUT);
	}

    gpio_pad_select_gpio(dconfig->cs);
    gpio_pad_select_gpio(dconfig->dc);

    gpio_set_direction(dconfig->cs, GPIO_MODE_OUTPUT);
    gpio_set_direction(dconfig->dc, GPIO_MODE_OUTPUT);

    gpio_set_level(dconfig->dc, 0);
    gpio_set_level(dconfig->cs, 1);
    if (dconfig->touch != TOUCH_TYPE_NONE) {
		gpio_pad_select_gpio(dconfig->tcs);
		gpio_set_direction(dconfig->tcs, GPIO_MODE_OUTPUT);
		gpio_set_level(dconfig->tcs, 1);
    }
    if (dconfig->bckl >= 0) {
		gpio_pad_select_gpio(dconfig->bckl);
		gpio_set_direction(dconfig->bckl, GPIO_MODE_OUTPUT);
		gpio_set_level(dconfig->bckl, dconfig->bckl_on ^ 1);
    }
    if (dconfig->rst >= 0) {
		gpio_pad_select_gpio(dconfig->rst);
		gpio_set_direction(dconfig->rst, GPIO_MODE_OUTPUT);
		gpio_set_level(dconfig->rst, 0);
    }
}

//-----------------------------------------------------
static esp_err_t TFT_spiInit(display_config_t *dconfig)
{
    esp_err_t ret;

    int used_spi = spi_host_used_by_sdspi();
    if ((used_spi != 0) && (used_spi == dconfig->host)) {
        // change spi host
        if (used_spi == VSPI_HOST) disp_spi->spihost = HSPI_HOST;
        else disp_spi->spihost = VSPI_HOST;
        ESP_LOGW(TAG, "spi bus changed (%d -> %d)", used_spi, disp_spi->spihost);
    }
    else disp_spi->spihost = dconfig->host;

    disp_spi->buscfg = SPIbus_configs[disp_spi->spihost];
    if (disp_spi->buscfg == NULL) {
        ESP_LOGE(TAG, "spi bus %d not available ", disp_spi->spihost);
        return ESP_ERR_INVALID_ARG;
    }

    disp_spi->dma_channel = 1;
    disp_spi->curr_clock = 8000000;							// for initialization set the clock to 8MHz
    disp_spi->handle = NULL;
    disp_spi->cs = dconfig->cs;
	disp_spi->dc = dconfig->dc;
	disp_spi->selected = 0;

    disp_spi->buscfg->miso_io_num = dconfig->miso;			// set SPI MISO pin
	disp_spi->buscfg->mosi_io_num = dconfig->mosi;			// set SPI MOSI pin
	disp_spi->buscfg->sclk_io_num = dconfig->sck;			// set SPI CLK pin
	disp_spi->buscfg->quadwp_io_num = -1;
	disp_spi->buscfg->quadhd_io_num = -1;
	disp_spi->buscfg->max_transfer_sz = 6*1024;

	disp_spi->devcfg.clock_speed_hz = disp_spi->curr_clock; // Initial clock
	disp_spi->devcfg.duty_cycle_pos = 128;                  // 50% duty cycle
	disp_spi->devcfg.mode=0;                                // SPI mode 0
	disp_spi->devcfg.spics_io_num = -1;          			// we will use external CS pin
	disp_spi->devcfg.queue_size=1;                          // we need only one transaction
	disp_spi->devcfg.flags=SPI_DEVICE_HALFDUPLEX;           // ALWAYS SET  to HALF DUPLEX MODE!! for display spi

	if (dconfig->touch != TOUCH_TYPE_NONE) {
	    ts_spi->spihost = disp_spi->spihost;				// Use the same spi bus as display
	    ts_spi->buscfg = SPIbus_configs[ts_spi->spihost];

	    ts_spi->dma_channel = 1;
	    ts_spi->handle = NULL;
		ts_spi->cs = dconfig->tcs; // external CS
		ts_spi->dc = -1;
		ts_spi->selected = 0;

		if (dconfig->touch == TOUCH_TYPE_STMPE610) {
		    ts_spi->curr_clock = 1000000;
			ts_spi->devcfg.clock_speed_hz = 1000000;   		// Clock 1 MHz
			ts_spi->devcfg.mode = STMPE610_SPI_MODE;   		// SPI mode 1
		}
		else {
		    ts_spi->curr_clock = 2500000;
			ts_spi->devcfg.clock_speed_hz = 2500000;        // Clock 2.5 MHz
			ts_spi->devcfg.mode = 0;                        //SPI mode 0
		}

		ts_spi->devcfg.duty_cycle_pos = 128;                // 50% duty cycle
		ts_spi->devcfg.spics_io_num = -1;					// External Touch CS pin (dconfig->tcs)
		ts_spi->devcfg.queue_size = 1;                      // we need only one transaction
		ts_spi->devcfg.flags = 0;
	}

    // ==================================================================
	// ==== Initialize the SPI bus and attach the LCD to the SPI bus ====
	ret = add_extspi_device(disp_spi);
	if (ret != ESP_OK) {
	    return ret;
	}

    ESP_LOGD(TAG, "spi bus configured (%d)", disp_spi->spihost);


	// ==== Test select/deselect ====
	ret = spi_device_select(disp_spi, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error selecting display device");
    }
	vTaskDelay(10 / portTICK_RATE_MS);
	ret = spi_device_deselect(disp_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error deselecting display device");
    }

    ESP_LOGI(TAG, "attached display device, speed=%u", spi_get_speed(disp_spi));
    ESP_LOGI(TAG, "bus uses native pins: %s", spi_uses_native_pins(disp_spi->handle) ? "true" : "false");

	if (dconfig->touch != TOUCH_TYPE_NONE) {
		// =====================================================
	    // ==== Attach the touch screen to the same SPI bus ====

		ret = add_extspi_device(ts_spi);
	    if (ret == ESP_OK) {
	        ESP_LOGD(TAG, "touch screen device added to spi bus (%d)", disp_spi->spihost);

			// ==== Test select/deselect ====
			ret = spi_device_select(ts_spi, 1);
		    if (ret != ESP_OK) {
		        ESP_LOGE(TAG, "Error selecting touch device");
		    }
			ret = spi_device_deselect(ts_spi);
		    if (ret != ESP_OK) {
		        ESP_LOGE(TAG, "Error deselecting touch device");
		    }

			ESP_LOGD(TAG, "attached TS device, speed=%u", spi_get_speed(ts_spi));
			if (dconfig->touch == TOUCH_TYPE_STMPE610) {
				stmpe610_Init();
				vTaskDelay(10 / portTICK_RATE_MS);
				uint32_t tver = stmpe610_getID();
				ESP_LOGW(TAG, "STMPE touch initialized, ver: %04x - %02x", tver >> 8, tver & 0xFF);
			}
	    }
	    else {
	        ESP_LOGE(TAG, "Error adding touch device to spi bus");
	    	ts_spi->handle = NULL;
	        dconfig->touch = TOUCH_TYPE_NONE;
	    }
	}
	return ESP_OK;
}

//=================================================
void TFT_display_setvars(display_config_t *dconfig)
{
    // === SET GLOBAL VARIABLES ==========================
    tft_disp_type = dconfig->type;
    tft_touch_type = dconfig->touch;
    _width = dconfig->width;
    _height = dconfig->height;
    // ==== Set maximum spi clock for display read    ====
    //      operations, function 'find_rd_speed()'    ====
    //      can be used after display initialization  ====
    max_rdclock = dconfig->rdspeed;
    TFT_RGB_BGR = dconfig->bgr;
    gamma_curve = dconfig->gamma;
    spi_speed = dconfig->speed;
    bits_per_color = dconfig->color_bits;
    invertrot = dconfig->invrot;
    // ===================================================
}

// Initialize the display
// ==================================================
esp_err_t TFT_display_init(display_config_t *dconfig)
{
    esp_err_t ret;
    TFT_PinsInit(dconfig);
    ret = TFT_spiInit(dconfig);
    if (ret != ESP_OK) return ret;

    TFT_display_setvars(dconfig);

    if (dconfig->rst >= 0) {
        //Reset the display
        gpio_set_level(dconfig->rst, 0);
        vTaskDelay(20 / portTICK_RATE_MS);
        gpio_set_level(dconfig->rst, 1);
        vTaskDelay(150 / portTICK_RATE_MS);
    }
    else {
        disp_spi_transfer_cmd_data(TFT_CMD_SWRESET, NULL, 0);
        vTaskDelay(200 / portTICK_RATE_MS);
    }

    disp_select();

    //Send all the initialization commands
    if ((tft_disp_type == DISP_TYPE_ILI9341) || (tft_disp_type == DISP_TYPE_M5STACK)) {
        if (dconfig->rst < 0) {
            vTaskDelay(50 / portTICK_RATE_MS);
        }
        commandList(ILI9341_init);
        _tft_setBitsPerColor(bits_per_color);
    }
    else if (tft_disp_type == DISP_TYPE_ILI9488) {
        commandList(ILI9488_init);
    }
    else if (tft_disp_type == DISP_TYPE_ST7789V) {
        commandList(ST7789V_init);
        _tft_setBitsPerColor(bits_per_color);
    }
    else if (tft_disp_type == DISP_TYPE_ST7735) {
        commandList(STP7735_init);
    }
    else if (tft_disp_type == DISP_TYPE_ST7735R) {
        commandList(STP7735R_init);
        commandList(Rcmd2green);
        commandList(Rcmd3);
    }
    else if (tft_disp_type == DISP_TYPE_ST7735B) {
        commandList(STP7735R_init);
        commandList(Rcmd2red);
        commandList(Rcmd3);
        uint8_t dt = 0xC0;
        disp_spi_transfer_cmd_data(TFT_MADCTL, &dt, 1);
    }
    else if (tft_disp_type == DISP_TYPE_GENERIC) {
        ESP_LOGW(TAG, "Generic display type, you must init the display manually");
        ///Enable backlight
        if (dconfig->bckl >= 0) {
            gpio_set_level(dconfig->bckl, dconfig->bckl_on);
        }
        return ESP_OK;
    }
    else {
        ESP_LOGE(TAG, "Display type %d not supported", tft_disp_type);
        disp_deselect();
        return ESP_FAIL;
    }

    disp_deselect();

    // Clear screen
    _tft_setRotation(PORTRAIT);
    TFT_pushColorRep(0, 0, _width-1, _height-1, (color_t){0,0,0}, (uint32_t)(_height*_width));

    ///Enable backlight
    if (dconfig->bckl >= 0) {
        gpio_set_level(dconfig->bckl, dconfig->bckl_on);
    }
    return ESP_OK;
}

#endif // CONFIG_MICROPY_USE_TFT

