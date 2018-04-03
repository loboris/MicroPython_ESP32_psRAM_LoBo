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


#include "sdkconfig.h"

#if CONFIG_MICROPY_USE_EVE_NOT_USE

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "FT8.h"
#include "tft/spi_master_lobo.h"


// FT8xx Memory Commands - use with FT8_memWritexx and FT8_memReadxx
#define MEM_WRITE	0x80	// FT8xx Host Memory Write
#define MEM_READ	0x00	// FT8xx Host Memory Read

uint16_t eve_cmdOffset = 0x0000;	// used to navigate command ring buffer
static uint8_t cmd_burst = 0;		// flag to indicate cmd-burst is active

spi_lobo_device_handle_t eve_spi = NULL;

//---------------------------------------------------------------------------------------------------
static void IRAM_ATTR _spi_transfer_start(spi_lobo_device_handle_t spi_dev, int wrbits, int rdbits) {
	// Load send buffer
	spi_dev->host->hw->user.usr_mosi_highpart = 0;
	spi_dev->host->hw->mosi_dlen.usr_mosi_dbitlen = wrbits-1;
	spi_dev->host->hw->user.usr_mosi = 1;
    if (rdbits) {
        spi_dev->host->hw->miso_dlen.usr_miso_dbitlen = rdbits;
        spi_dev->host->hw->user.usr_miso = 1;
    }
    else {
        spi_dev->host->hw->miso_dlen.usr_miso_dbitlen = 0;
        spi_dev->host->hw->user.usr_miso = 0;
    }
	// Start transfer
	spi_dev->host->hw->cmd.usr = 1;
    // Wait for SPI bus ready
	while (spi_dev->host->hw->cmd.usr);
}


//-----------------------------
void FT8_cmdWrite(uint8_t data)
{
	if (spi_lobo_device_select(eve_spi, 0)) return;
	eve_spi->host->hw->data_buf[0] = (uint32_t)data;
    _spi_transfer_start(eve_spi, 8, 0);
    spi_lobo_device_deselect(eve_spi);
}

//----------------------------------------------------
uint32_t FT8_address(uint32_t ftAddress, uint8_t mask)
{
	uint32_t wd = (ftAddress >> 16) & 0x3F;	// Memory Write plus high address byte
	wd |= ftAddress & 0x0000FF00;			// middle address byte
	wd |= (ftAddress << 16) & 0x00FF0000;	// low address byte & dummy byte
	return wd;
}

//--------------------------------------
uint8_t FT8_memRead8(uint32_t ftAddress)
{
	uint8_t ftData8 = 0;

	if (spi_lobo_device_select(eve_spi, 0)) return 0;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_READ);
	eve_spi->host->hw->data_buf[1] = 0;
    _spi_transfer_start(eve_spi, 40, 40);

    ftData8 = (uint8_t)eve_spi->host->hw->data_buf[1];
    spi_lobo_device_deselect(eve_spi);

    return ftData8;	// return byte read
}

//----------------------------------------
uint16_t FT8_memRead16(uint32_t ftAddress)
{
	uint16_t ftData16 = 0;

	if (spi_lobo_device_select(eve_spi, 0)) return 0;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_READ);
	eve_spi->host->hw->data_buf[1] = 0;
    _spi_transfer_start(eve_spi, 48, 48);

    ftData16 = (uint16_t)eve_spi->host->hw->data_buf[1];
    spi_lobo_device_deselect(eve_spi);

	return ftData16;	// return integer read
}

//----------------------------------------
uint32_t FT8_memRead32(uint32_t ftAddress)
{
	uint32_t ftData32= 0;

	if (spi_lobo_device_select(eve_spi, 0)) return 0;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_READ);
	eve_spi->host->hw->data_buf[1] = 0;
    _spi_transfer_start(eve_spi, 56, 56);

    ftData32 = eve_spi->host->hw->data_buf[1];
    spi_lobo_device_deselect(eve_spi);

	return ftData32;	// return long read
}

//-----------------------------------------------------
void FT8_memWrite8(uint32_t ftAddress, uint8_t ftData8)
{
	if (spi_lobo_device_select(eve_spi, 0)) return;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE) | (uint32_t)(ftData8 << 24);
    _spi_transfer_start(eve_spi, 32, 32);
    spi_lobo_device_deselect(eve_spi);
}

//--------------------------------------------------------
void FT8_memWrite16(uint32_t ftAddress, uint16_t ftData16)
{
	if (spi_lobo_device_select(eve_spi, 0)) return;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE) | (uint32_t)(ftData16 << 24);
	eve_spi->host->hw->data_buf[1] = (uint32_t)(ftData16 >> 8);
    _spi_transfer_start(eve_spi, 40, 40);
    spi_lobo_device_deselect(eve_spi);
}

//--------------------------------------------------------
void FT8_memWrite32(uint32_t ftAddress, uint32_t ftData32)
{
	if (spi_lobo_device_select(eve_spi, 0)) return;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE) | ftData32 << 24;
	uint32_t wd = ((ftData32 >> 8) & 0xFF) | ((ftData32 >> 16) & 0xFF) | ((ftData32 >> 24) & 0xFF);
	eve_spi->host->hw->data_buf[1] = wd;
    _spi_transfer_start(eve_spi, 56, 56);
    spi_lobo_device_deselect(eve_spi);
}

//----------------------------------------------------
static void FT8_send_data(uint8_t *data, uint32_t len)
{
	spi_lobo_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    t.length = len * 8;
    t.tx_buffer = data;
    t.rxlength = 0;
    t.rx_buffer = NULL;

    spi_lobo_transfer_data(eve_spi, &t); // Send using direct mode
}

//-----------------------------------------------------------------------------------
void FT8_memWrite_flash_buffer(uint32_t ftAddress, const uint8_t *data, uint16_t len)
{
	if (spi_lobo_device_select(eve_spi, 0)) return;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
    _spi_transfer_start(eve_spi, 24, 24);

	len = (len + 3)&(~3);

	FT8_send_data((uint8_t *)data, len);

    spi_lobo_device_deselect(eve_spi);
}


// Check if the graphics processor completed executing the current command list.
// This is the case when REG_CMD_READ matches eve_cmdOffset, indicating that all commands have been executed.
//--------------------
uint8_t FT8_busy(void)
{
	uint16_t cmdBufferRead;

	cmdBufferRead = FT8_memRead16(REG_CMD_READ);	// read the graphics processor read pointer

	if (eve_cmdOffset != cmdBufferRead) return 1;
	else return 0;
}

//------------------------------
uint32_t FT8_get_touch_tag(void)
{
	uint32_t value;

	value = FT8_memRead32(REG_TOUCH_TAG);
	return value;
}


// Order the command coprocessor to start processing its FIFO queue and do not wait for completion
//----------------------
void FT8_cmd_start(void)
{
	uint32_t ftAddress = REG_CMD_WRITE;
	FT8_memWrite16(ftAddress, eve_cmdOffset);
}


/// Order the command coprocessor to start processing its FIFO queue and wait for completion
//------------------------
void FT8_cmd_execute(void)
{
	FT8_cmd_start();
	while (FT8_busy());
}

//--------------------------
void FT8_get_cmdoffset(void)
{
	eve_cmdOffset = FT8_memRead16(REG_CMD_WRITE);
}

//----------------------------------------
void FT8_inc_cmdoffset(uint16_t increment)
{
	eve_cmdOffset += increment;
	eve_cmdOffset &= 0x0fff;
}


/*
These eliminate the overhead of transmitting the command fifo address with every single command, just wrap a sequence of commands
with these and the address is only transmitted once at the start of the block.
Be careful to not use any functions in the sequence that do not address the command-fifo as for example any FT8_mem...() function.
*/
//----------------------------
void FT8_start_cmd_burst(void)
{
	uint32_t ftAddress;
	
	cmd_burst = 42;
	ftAddress = FT8_RAM_CMD + eve_cmdOffset;
	if (spi_lobo_device_select(eve_spi, 0)) return;
	eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
    _spi_transfer_start(eve_spi, 24, 24);
}

//--------------------------
void FT8_end_cmd_burst(void)
{
	cmd_burst = 0;
    spi_lobo_device_deselect(eve_spi);
}

// Begin a coprocessor command
//----------------------------------
void FT8_start_cmd(uint32_t command)
{
	uint32_t ftAddress;
	
	uint32_t wd = command & 0xff;
	wd |= (uint32_t)((command >> 8) & 0xff);
	wd |= (uint32_t)((command >> 16) & 0xff);
	wd |= (uint32_t)((command >> 24) & 0xff);

	if (cmd_burst == 0)	{
		ftAddress = FT8_RAM_CMD + eve_cmdOffset;
		if (spi_lobo_device_select(eve_spi, 0)) return;
		eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
	    _spi_transfer_start(eve_spi, 24, 24); // send address
	}

	eve_spi->host->hw->data_buf[0] = wd;
    _spi_transfer_start(eve_spi, 32, 32);

	FT8_inc_cmdoffset(4);	// update the command-ram pointer
}


/* === generic function for all commands that have no arguments and all display-list specific control words ===
 *
 examples:
 FT8_cmd_dl(CMD_DLSTART);
 FT8_cmd_dl(CMD_SWAP);
 FT8_cmd_dl(CMD_SCREENSAVER);
 FT8_cmd_dl(LINE_WIDTH(1*16));
 FT8_cmd_dl(VERTEX2F(0,0));
 FT8_cmd_dl(DL_BEGIN | FT8_RECTS);
 */
//-------------------------------
void FT8_cmd_dl(uint32_t command)
{
	FT8_start_cmd(command);
	if (cmd_burst == 0) {
	    spi_lobo_device_deselect(eve_spi);
	}
}


// Write a string to coprocessor memory in context of a command
// no chip-select, just plain spi-transfers
//-------------------------------------
void FT8_write_string(const char *text)
{
	uint8_t textlen = strlen(text);
	uint8_t padding = 0;

	spi_lobo_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    t.length = textlen * 8;
    t.tx_buffer = text;
    t.rxlength = 0;
    t.rx_buffer = NULL;

    spi_lobo_transfer_data(eve_spi, &t); // Send using direct mode

	padding = strlen(text) % 4; /* 0, 1, 2 or 3 */
	padding = 4-padding;		/* 4, 3, 2, 1 */

	eve_spi->host->hw->data_buf[0] = 0;
    _spi_transfer_start(eve_spi, padding * 8, 0);

	FT8_inc_cmdoffset(textlen + padding);
}

// ============================================
// ==== Commands to draw graphics objects: ====
// ============================================

//-------------------------------------------------------------------------------------------------------------------------------------------
static void FT8_send_params(int16_t p1, int16_t p2, int16_t p3, uint16_t p4, uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8, uint8_t len)
{
	uint32_t wd = (uint32_t)(p1 & 0xFF);
	wd |= (uint32_t)(p1 & 0xFF00);
	wd |= (uint32_t)((p2 & 0xFF) << 16);
	wd |= (uint32_t)((p2 & 0xFF00) << 16);
	eve_spi->host->hw->data_buf[0] = wd;
	if (len < 4) goto send;

	wd = (uint32_t)(p3 & 0xFF);
	wd |= (uint32_t)(p3 & 0xFF00);
	wd |= (uint32_t)((p4 & 0xFF) << 16);
	wd |= (uint32_t)((p4 & 0xFF00) << 16);
	eve_spi->host->hw->data_buf[1] = wd;
	if (len < 6) goto send;

	wd = (uint32_t)(p5 & 0xFF);
	wd |= (uint32_t)(p5 & 0xFF00);
	wd |= (uint32_t)((p6 & 0xFF) << 16);
	wd |= (uint32_t)((p6 & 0xFF00) << 16);
	eve_spi->host->hw->data_buf[2] = wd;
	if (len < 8) goto send;

	wd = (uint32_t)(p7 & 0xFF);
	wd |= (uint32_t)(p7 & 0xFF00);
	wd |= (uint32_t)((p8 & 0xFF) << 16);
	wd |= (uint32_t)((p8 & 0xFF00) << 16);
	eve_spi->host->hw->data_buf[2] = wd;

send:
    _spi_transfer_start(eve_spi, len*16, len*16); // send

	FT8_inc_cmdoffset(len * 2);
}

//---------------------------------------------------------------------------------
static void FT8_send_long(uint32_t val1, uint32_t val2, uint32_t val3, uint8_t len)
{
	uint32_t wd = (uint32_t)(val1 & 0xFF);
	wd |= (uint32_t)((val1 >> 8) & 0xFF);
	wd |= (uint32_t)((val1 >> 16) & 0xFF);
	wd |= (uint32_t)((val1 >> 24) & 0xFF);
	eve_spi->host->hw->data_buf[0] = wd;
	if (len < 2) goto send;

	wd = (uint32_t)(val2 & 0xFF);
	wd |= (uint32_t)((val2 >> 8) & 0xFF);
	wd |= (uint32_t)((val2 >> 16) & 0xFF);
	wd |= (uint32_t)((val2 >> 24) & 0xFF);
	eve_spi->host->hw->data_buf[1] = wd;
	if (len < 3) goto send;

	wd = (uint32_t)(val3 & 0xFF);
	wd |= (uint32_t)((val3 >> 8) & 0xFF);
	wd |= (uint32_t)((val3 >> 16) & 0xFF);
	wd |= (uint32_t)((val3 >> 24) & 0xFF);
	eve_spi->host->hw->data_buf[2] = wd;

send:
    _spi_transfer_start(eve_spi, len*32, len*32); // send

	FT8_inc_cmdoffset(len * 4);
}

// Draw text
//-----------------------------------------------------------------------------------------
void FT8_cmd_text(int16_t x0, int16_t y0, int16_t font, uint16_t options, const char* text)
{
	FT8_start_cmd(CMD_TEXT);
	FT8_send_params(x0, y0, font, options, 0, 0, 0, 0, 4);
	FT8_write_string(text);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

// Draw a button with text
//-------------------------------------------------------------------------------------------------------------------
void FT8_cmd_button(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t font, uint16_t options, const char* text)
{
	FT8_start_cmd(CMD_BUTTON);
	FT8_send_params(x0, y0, w0, h0, font, options, 0, 0, 6);
	FT8_write_string(text);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

// Draw a clock
//----------------------------------------------------------------------------------------------------------------------------------------------
void FT8_cmd_clock(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t hours, uint16_t minutes, uint16_t seconds, uint16_t millisecs)
{
	FT8_start_cmd(CMD_CLOCK);
	FT8_send_params(x0, y0, r0, options, hours, minutes, seconds, millisecs, 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//----------------------------------
void FT8_cmd_bgcolor(uint32_t color)
{
	FT8_start_cmd(CMD_BGCOLOR);
	FT8_send_long(color, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//----------------------------------
void FT8_cmd_fgcolor(uint32_t color)
{
	FT8_start_cmd(CMD_FGCOLOR);
	FT8_send_long(color, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//------------------------------------
void FT8_cmd_gradcolor(uint32_t color)
{
	FT8_start_cmd(CMD_GRADCOLOR);
	FT8_send_long(color, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//------------------------------------------------------------------------------------------------------------------------------------
void FT8_cmd_gauge(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t major, uint16_t minor, uint16_t val, uint16_t range)
{
	FT8_start_cmd(CMD_GAUGE);
	FT8_send_params(x0, y0, r0, options, major, minor, val, range, 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-------------------------------------------------------------------------------------------------
void FT8_cmd_gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1)
{
	FT8_start_cmd(CMD_GRADIENT);
	FT8_send_params(x0, y0, (uint16_t)rgb0, (uint16_t)(rgb0 >> 16), x1, y1, (uint16_t)rgb1, (uint16_t)(rgb1 >> 16), 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-----------------------------------------------------------------------------------------------------------------
void FT8_cmd_keys(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t font, uint16_t options, const char* text)
{
	FT8_start_cmd(CMD_KEYS);

	FT8_send_params(x0, y0, w0, h0, font, options, 0, 0, 6);
	FT8_write_string(text);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-------------------------------------------------------------------------------------------------------------------
void FT8_cmd_progress(int16_t x0, int16_t y0, int16_t w0, int16_t h0, uint16_t options, uint16_t val, uint16_t range)
{
	FT8_start_cmd(CMD_PROGRESS);
	FT8_send_params(x0, y0, w0, h0, options, val, range, 0, 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-----------------------------------------------------------------------------------------------------------------------------------
void FT8_cmd_scrollbar(int16_t x0, int16_t y0, int16_t w0, int16_t h0, uint16_t options, uint16_t val, uint16_t size, uint16_t range)
{
	FT8_start_cmd(CMD_SCROLLBAR);
	FT8_send_params(x0, y0, w0, h0, options, val, size, range, 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


void FT8_cmd_slider(int16_t x1, int16_t y1, int16_t w1, int16_t h1, uint16_t options, uint16_t val, uint16_t range)
{
	FT8_start_cmd(CMD_SLIDER);
	FT8_send_params(x1, y1, w1, h1, options, val, range, 0, 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-----------------------------------------------------------------------------------
void FT8_cmd_dial(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t val)
{
	FT8_start_cmd(CMD_DIAL);
	FT8_send_params(x0, y0, r0, options, val, 0, 0, 0, 6);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-----------------------------------------------------------------------------------------------------------------------
void FT8_cmd_toggle(int16_t x0, int16_t y0, int16_t w0, int16_t font, uint16_t options, uint16_t state, const char* text)
{
	FT8_start_cmd(CMD_TOGGLE);
	FT8_send_params(x0, y0, w0, font, options, state, 0, 0, 6);
	FT8_write_string(text);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


#ifdef FT8_81X_ENABLE
//---------------------------------
void FT8_cmd_setbase(uint32_t base)
{
	FT8_start_cmd(CMD_SETBASE);
	FT8_send_params((uint16_t)base, (uint16_t)(base >> 16), 0, 0, 0, 0, 0, 0, 2);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//----------------------------------------------------------------------------------
void FT8_cmd_setbitmap(uint32_t addr, uint16_t fmt, uint16_t width, uint16_t height)
{
	FT8_start_cmd(CMD_SETBITMAP);
	FT8_send_params((uint16_t)addr, (uint16_t)(addr >> 16), fmt, width, height, 0, 0, 0, 6);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}
#endif

//-----------------------------------------------------------------------------------------
void FT8_cmd_number(int16_t x0, int16_t y0, int16_t font, uint16_t options, int32_t number)
{
	FT8_start_cmd(CMD_NUMBER);
	FT8_send_params(x0, y0, font, options, (uint16_t)number, (uint16_t)(number >> 16), 0, 0, 6);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

// ========================================
// ==== Commands to operate on memory: ====
// ========================================

void FT8_cmd_memzero(uint32_t ptr, uint32_t num)
{
	FT8_start_cmd(CMD_MEMZERO);
	FT8_send_long(ptr, num, 0, 2);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


void FT8_cmd_memset(uint32_t ptr, uint8_t value, uint32_t num)
{
	FT8_start_cmd(CMD_MEMSET);
	FT8_send_long(ptr, (uint32_t)value, num, 3);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


/*
void FT8_cmd_memwrite(uint32_t dest, uint32_t num, const uint8_t *data)
{
	FT8_start_cmd(CMD_MEMWRITE);

	spi_transmit((uint8_t)(dest));
	spi_transmit((uint8_t)(dest >> 8));
	spi_transmit((uint8_t)(dest >> 16));
	spi_transmit((uint8_t)(dest >> 24));

	spi_transmit((uint8_t)(num));
	spi_transmit((uint8_t)(num >> 8));
	spi_transmit((uint8_t)(num >> 16));
	spi_transmit((uint8_t)(num >> 24));

	num = (num + 3)&(~3);

	for(count=0;count<len;count++)
	{
		spi_transmit(pgm_read_byte_far(data+count));
	}

	FT8_inc_cmdoffset(8+len);

	if(cmd_burst == 0)
	{
		FT8_cs_clear();
	}
}
*/

//------------------------------------------------------------
void FT8_cmd_memcpy(uint32_t dest, uint32_t src, uint32_t num)
{
	FT8_start_cmd(CMD_MEMCPY);
	FT8_send_long(dest, src, num, 3);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//---------------------------------------------
void FT8_cmd_append(uint32_t ptr, uint32_t num)
{
	FT8_start_cmd(CMD_APPEND);
	FT8_send_long(ptr, num, 0, 2);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

// ============================================================
// ==== Commands for loading image data into FT8xx memory: ====
// ============================================================


void spi_flash_write(const uint8_t *data, uint16_t len)
{
	FT8_send_data((uint8_t *)data, len);
	FT8_inc_cmdoffset(len);
}

//-------------------------------------------------------------------
void FT8_cmd_inflate(uint32_t ptr, const uint8_t *data, uint16_t len)
{
	FT8_start_cmd(CMD_INFLATE);
	FT8_send_long(ptr, 0, 0, 1);

	spi_flash_write(data, len);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

// This is meant to be called outside display-list building,
// it includes executing the command and waiting for completion, does not support cmd-burst
//---------------------------------------------------------------------------------------
void FT8_cmd_loadimage(uint32_t ptr, uint32_t options, const uint8_t *data, uint16_t len)
{
	uint16_t bytes_left;
	uint16_t block_len;
	uint32_t ftAddress;
	
	FT8_start_cmd(CMD_LOADIMAGE);
	FT8_send_long(ptr, options, 0, 2);

	spi_lobo_device_deselect(eve_spi);

	#ifdef FT8_81X_ENABLE
	if((options & FT8_OPT_MEDIAFIFO) == 0) // data is not transmitted thru the Media-FIFO
	#endif
	{
		bytes_left = len;
		while(bytes_left > 0)
		{
			block_len = bytes_left>4000 ? 4000:bytes_left;

			ftAddress = FT8_RAM_CMD + eve_cmdOffset;
			spi_lobo_device_select(eve_spi, 0);

			eve_spi->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
		    _spi_transfer_start(eve_spi, 24, 24); // send address

		    spi_flash_write(data, block_len);
			spi_lobo_device_deselect(eve_spi);

			data += block_len;
			bytes_left -= block_len;
			FT8_cmd_execute();
		}
	}
}


#ifdef FT8_81X_ENABLE
// this is meant to be called outside display-list building,
// does not support cmd-burst
//-------------------------------------------------
void FT8_cmd_mediafifo(uint32_t ptr, uint32_t size)
{
	FT8_start_cmd(CMD_MEDIAFIFO);
	FT8_send_long(ptr, size, 0, 2);

	spi_lobo_device_deselect(eve_spi);
}
#endif


// ===========================================================
// ==== Commands for setting the bitmap transform matrix: ====
// ===========================================================

//--------------------------------------------
void FT8_cmd_translate(int32_t tx, int32_t ty)
{
	FT8_start_cmd(CMD_TRANSLATE);
	FT8_send_long(tx, ty, 0, 2);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//----------------------------------------
void FT8_cmd_scale(int32_t sx, int32_t sy)
{
	FT8_start_cmd(CMD_SCALE);
	FT8_send_long(sx, sy, 0, 2);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//------------------------------
void FT8_cmd_rotate(int32_t ang)
{
	FT8_start_cmd(CMD_ROTATE);
	FT8_send_long(ang, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


/*
 * 	the description in the programmers guide is strange for this function
	while it is named *get*matrix, parameters 'a' to 'f' are supplied to
	the function and described as "output parameter"
	best guess is that this one allows to setup the matrix coefficients manually
	instead automatically like with _translate, _scale and _rotate
	if this assumption is correct it rather should be named cmd_setupmatrix()
 */
//--------------------------------------------------------------------------------------
void FT8_cmd_getmatrix(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f)
{
	FT8_start_cmd(CMD_SETMATRIX);
	FT8_send_long(a, b, c, 3);
	FT8_send_long(d, e, f, 3);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


// =========================
// ==== Other commands: ====
// =========================

//--------------------------
void FT8_cmd_calibrate(void)
{
	FT8_start_cmd(CMD_CALIBRATE);
	FT8_send_long(0, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//---------------------------------
void FT8_cmd_interrupt(uint32_t ms)
{
	FT8_start_cmd(CMD_INTERRUPT);
	FT8_send_long(ms, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


#ifdef FT8_81X_ENABLE
//---------------------------------------------------
void FT8_cmd_romfont(uint32_t font, uint32_t romslot)
{
	FT8_start_cmd(CMD_ROMFONT);
	FT8_send_long(font, romslot & 0xFFFF, 0, 2);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}
#endif

//-----------------------------------------------
void FT8_cmd_setfont(uint32_t font, uint32_t ptr)
{
	FT8_start_cmd(CMD_SETFONT);
	FT8_send_long(font, ptr, 0, 2);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


#ifdef FT8_81X_ENABLE
//--------------------------------------------------------------------
void FT8_cmd_setfont2(uint32_t font, uint32_t ptr, uint32_t firstchar)
{
	FT8_start_cmd(CMD_SETFONT2);
	FT8_send_long(font, ptr, firstchar, 3);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//--------------------------------
void FT8_cmd_setrotate(uint32_t r)
{
	FT8_start_cmd(CMD_SETROTATE);
	FT8_send_long(r, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//--------------------------------------
void FT8_cmd_setscratch(uint32_t handle)
{
	FT8_start_cmd(CMD_SETSCRATCH);
	FT8_send_long(handle, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}
#endif

//--------------------------------------------------------------------------------------------------
void FT8_cmd_sketch(int16_t x0, int16_t y0, uint16_t w0, uint16_t h0, uint32_t ptr, uint16_t format)
{
	FT8_start_cmd(CMD_SKETCH);
	FT8_send_params(x0, y0, w0, h0, (uint16_t)ptr, (uint16_t)(ptr >> 16), 0, 0, 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//---------------------------------
void FT8_cmd_snapshot(uint32_t ptr)
{
	FT8_start_cmd(CMD_SNAPSHOT);
	FT8_send_long(ptr, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


#ifdef FT8_81X_ENABLE
//------------------------------------------------------------------------------------------------
void FT8_cmd_snapshot2(uint32_t fmt, uint32_t ptr, int16_t x0, int16_t y0, int16_t w0, int16_t h0)
{
	FT8_start_cmd(CMD_SNAPSHOT2);
	FT8_send_params((uint16_t)fmt, (uint16_t)(fmt >> 16), (uint16_t)ptr, (uint16_t)(ptr >> 16), x0, y0, w0, h0, 8);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}
#endif

//--------------------------------------------------------------------------
void FT8_cmd_spinner(int16_t x0, int16_t y0, uint16_t style, uint16_t scale)
{
	FT8_start_cmd(CMD_SPINNER);
	FT8_send_params(x0, y0, style, scale, 0, 0, 0, 0, 4);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-----------------------------------------------------------------------------
void FT8_cmd_track(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t tag)
{
	FT8_start_cmd(CMD_TRACK);
	FT8_send_params(x0, y0, w0, h0, tag, 0, 0, 0, 6);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


// ====================================================================
// ==== Commands that return values by writing to the command-fifo ====
// ====================================================================


/*
 * This is handled by having this functions return the offset address on the command-fifo from
 * which the results can be fetched after execution: FT8_memRead32(FT8_RAM_CMD + offset)
 *
 * Note: yes, these are different than the functions in the Programmers Guide from FTDI,
	this is because I have no idea why anyone would want to pass "result" as an actual argument to these functions
	when this only marks the offset the command-processor is writing to,
	it may even be okay to not transfer anything at all,
	just advance the offset by 4 bytes

 * Example of using FT8_cmd_memcrc:

   offset = FT8_cmd_memcrc(my_ptr_to_some_memory_region, some_amount_of_bytes);
   FT8_cmd_execute();
   crc32 = FT8_memRead32(FT8_RAM_CMD + offset);

*/

//-------------------------------------------------
uint16_t FT8_cmd_memcrc(uint32_t ptr, uint32_t num)
{
	uint16_t offset;

	FT8_start_cmd(CMD_MEMCRC);
	FT8_send_long(ptr, num, 0, 3);
	offset = eve_cmdOffset - 4;

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);

	return offset;
}

//---------------------------
uint16_t FT8_cmd_getptr(void)
{
	uint16_t offset;

	FT8_start_cmd(CMD_GETPTR);
	FT8_send_long(0, 0, 0, 1);
	offset = eve_cmdOffset - 4;

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);

	return offset;
}

//------------------------------------
uint16_t FT8_cmd_regread(uint32_t ptr)
{
	uint16_t offset;

	FT8_start_cmd(CMD_REGREAD);
	FT8_send_long(ptr, 0, 0, 2);
	offset = eve_cmdOffset - 4;

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);

	return offset;
}


/*
 * Be aware that this returns the first offset pointing to "width",
 * in order to also read "height" you need to:

   offset = FT8_cmd_getprops(my_last_picture_pointer);
   FT8_cmd_execute();
   width = FT8_memRead32(FT8_RAM_CMD + offset);
   offset += 4;
   offset &= 0x0fff;
   height = FT8_memRead32(FT8_RAM_CMD + offset);
*/

//-------------------------------------
uint16_t FT8_cmd_getprops(uint32_t ptr)
{
	uint16_t offset;

	FT8_start_cmd(CMD_REGREAD);
	FT8_send_long(ptr, 0, 0, 3);
	offset = eve_cmdOffset - 8;
	eve_cmdOffset -= 4;
	eve_cmdOffset &= 0x0fff;

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);

	return offset;
}

// ========================================================================================================================================
// ==== meta-commands, sequences of several display-list entries condensed into simpler to use functions at the price of some overhead ====
// ========================================================================================================================================

//-------------------------------------------------------
void FT8_cmd_point(int16_t x0, int16_t y0, uint16_t size)
{
	uint32_t calc1, calc2;

	FT8_start_cmd((DL_BEGIN | FT8_POINTS));

	calc1 = POINT_SIZE(size*16);
	calc2 = VERTEX2F(x0 * 16, y0 * 16);
	FT8_send_long(calc1, calc2, DL_END, 3);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//-------------------------------------------------------------------------------
void FT8_cmd_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t width)
{
	uint32_t calc1, calc2,calc3;

	FT8_start_cmd((DL_BEGIN | FT8_LINES));

	calc1 = LINE_WIDTH(width * 16);
	calc2 = VERTEX2F(x0 * 16, y0 * 16);
	calc3 = VERTEX2F(x1 * 16, y1 * 16);
	FT8_send_long(calc1, calc2, calc3, 3);
	FT8_send_long(DL_END, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}

//--------------------------------------------------------------------------------
void FT8_cmd_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t corner)
{
	uint32_t calc1, calc2, calc3;

	FT8_start_cmd((DL_BEGIN | FT8_RECTS));

	calc1 = LINE_WIDTH(corner * 16);
	calc2 = VERTEX2F(x0 * 16, y0 * 16);
	calc3 = VERTEX2F(x1 * 16, y1 * 16);
	FT8_send_long(calc1, calc2, calc3, 3);
	FT8_send_long(DL_END, 0, 0, 1);

	if (cmd_burst == 0) spi_lobo_device_deselect(eve_spi);
}


// ============================================================================================
// ==== init, has to be executed with the SPI setup to 11 MHz or less as required by FT8xx ====
// ============================================================================================

uint8_t FT8_init(void)
{
	uint8_t gpio;
	uint8_t chipid;
	uint8_t timeout = 0;

	//FT8_pdn_set();
    vTaskDelay(10 / portTICK_RATE_MS); // minimum time for power-down is 5ms
	//FT8_pdn_clear();
    vTaskDelay(21 / portTICK_RATE_MS); // minimum time to allow from rising PD_N to first access is 20ms
	
    //	FT8_cmdWrite(FT8_CORERST);	// reset, only required for warmstart if PowerDown line is not used

	if (FT8_HAS_CRYSTAL != 0) FT8_cmdWrite(FT8_CLKEXT);	// setup FT8xx for external clock
	else FT8_cmdWrite(FT8_CLKINT);						// setup FT8xx for internal clock

	FT8_cmdWrite(FT8_ACTIVE);	// start FT8xx

	chipid = FT8_memRead8(REG_ID);	// Read ID register
	// if chip id is not 0x7c, continue to read it until it is,
	// FT81x may need a moment for it's power on self test
	while(chipid != 0x7C) {
		chipid = FT8_memRead8(REG_ID);
	    vTaskDelay(2 / portTICK_RATE_MS);
		timeout++;
		if (timeout > 100) return 0;
	}

	FT8_memWrite8(REG_PCLK, 0x00);				// set PCLK to zero - don't clock the LCD until later
	FT8_memWrite8(REG_PWM_DUTY, 10);			// turn off backlight

	// *** Initialize Display
	FT8_memWrite16(REG_HSIZE,   FT8_HSIZE);		// active display width
	FT8_memWrite16(REG_HCYCLE,  FT8_HCYCLE);	// total number of clocks per line, including front/back porch
	FT8_memWrite16(REG_HOFFSET, FT8_HOFFSET);	// start of active line
	FT8_memWrite16(REG_HSYNC0,  FT8_HSYNC0);	// start of horizontal sync pulse
	FT8_memWrite16(REG_HSYNC1,  FT8_HSYNC1);	// end of horizontal sync pulse
	FT8_memWrite16(REG_VSIZE,   FT8_VSIZE);		// active display height
	FT8_memWrite16(REG_VCYCLE,  FT8_VCYCLE);	// total number of lines per screen, incl pre/post
	FT8_memWrite16(REG_VOFFSET, FT8_VOFFSET);	// start of active screen
	FT8_memWrite16(REG_VSYNC0,  FT8_VSYNC0);	// start of vertical sync pulse
	FT8_memWrite16(REG_VSYNC1,  FT8_VSYNC1);	// end of vertical sync pulse
	FT8_memWrite8(REG_SWIZZLE,  FT8_SWIZZLE);	// FT8xx output to LCD - pin order
	FT8_memWrite8(REG_PCLK_POL, FT8_PCLKPOL);	/// LCD data is clocked in on this PCLK edge
	// Don't set PCLK yet - wait for just after the first display list

	// ** Configure Touch
	FT8_memWrite8(REG_TOUCH_MODE, FT8_TMODE_CONTINUOUS);	// enable touch
	FT8_memWrite16(REG_TOUCH_RZTHRESH, FT8_TOUCH_RZTHRESH);	// eliminate any false touches

	// ** Configure Audio - not used, so disable it
	FT8_memWrite8(REG_VOL_PB, 0x00);		// turn recorded audio volume down
	//	FT8_memWrite8(REG_VOL_SOUND, 0xff);	// turn synthesizer volume on
	FT8_memWrite8(REG_VOL_SOUND, 0x00);		// turn synthesizer volume off
	FT8_memWrite16(REG_SOUND, 0x6000);		//	set synthesizer to mute

	FT8_memWrite32(FT8_RAM_DL, DL_CLEAR_RGB);
	FT8_memWrite32(FT8_RAM_DL + 4, (DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG));
	FT8_memWrite32(FT8_RAM_DL + 8, DL_DISPLAY);	// end of display list
	FT8_memWrite32(REG_DLSWAP, FT8_DLSWAP_FRAME);

	// nothing is being displayed yet... the pixel clock is still 0x00

	gpio = FT8_memRead8(REG_GPIO_DIR);
	// set DISP to Output although it always is output, set GPIO1 to Output - Audio Enable on VM800B
	gpio |= 0x82;
	FT8_memWrite8(REG_GPIO_DIR, gpio);

	gpio = FT8_memRead8(REG_GPIO);		// read the FT8xx GPIO register for a read/modify/write operation
	//	gpio |= 0x82;					// set bit 7 of FT8xx GPIO register (DISP), set GPIO1 to High to enable Audio - others are inputs
	gpio |= 0x80;						// set bit 7 of FT8xx GPIO register (DISP), others are inputs
	FT8_memWrite8(REG_GPIO, gpio);		// enable the DISP signal to the LCD panel
	FT8_memWrite8(REG_PCLK, FT8_PCLK);	// now start clocking data to the LCD panel

	FT8_memWrite8(REG_PWM_DUTY, 70);	// turn on backlight

    vTaskDelay(2 / portTICK_RATE_MS);	// just to be safe
	
	while (FT8_busy() == 1) ;

	FT8_get_cmdoffset();
	return 1;
}

#endif // CONFIG_MICROPY_USE_EVE
