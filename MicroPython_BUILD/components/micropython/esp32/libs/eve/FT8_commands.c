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

#if CONFIG_MICROPY_USE_EVE

#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "FT8.h"
#include "driver/spi_master_utils.h"

#include "mphalport.h"

uint16_t eve_chip_id = 0;

static const char TAG[] = "[EveDrv]";

// FT8xx Memory Commands - use with FT8_memWritexx and FT8_memReadxx
#define MEM_WRITE	    0x80	// FT8xx Host Memory Write
#define MEM_READ	    0x00    // FT8xx Host Memory Read

#define MIN_FIFO_FREE   256

uint16_t eve_cmdOffset = 0x0000;	// used to navigate command ring buffer
static uint8_t cmd_burst = 0;		// flag to indicate cmd-burst is active

uint8_t eve_spibus_is_init = 0;
uint8_t spi_is_init = 0;
uint8_t ft8_full_cs = 1;
exspi_device_handle_t *eve_spi = NULL;
uint32_t ft8_ramg_ptr = FT8_RAM_G;
FT8_Fifo_t ft8_stFifo = {0};
//FT8_Fifo_t *ft8_pFifo = &ft8_stFifo;


#ifdef CONFIG_EVE_CHIP_TYPE1
// FT811 / FT813 binary-blob from FTDIs AN_336 to patch the touch-engine for Goodix GT911 / GT9271 touch controllers
const uint16_t FT8_GT911_len = 1184;
const uint8_t FT8_GT911_data[1184] =
{
 26,255,255,255,32,32,48,0,4,0,0,0,2,0,0,0,
 34,255,255,255,0,176,48,0,120,218,237,84,221,111,84,69,20,63,51,179,93,160,148,101,111,76,5,44,141,123,111,161,11,219,154,16,9,16,17,229,156,75,26,11,13,21,227,3,16,252,184,179,
 45,219,143,45,41,125,144,72,67,100,150,71,189,113,18,36,17,165,100,165,198,16,32,17,149,196,240,128,161,16,164,38,54,240,0,209,72,130,15,38,125,48,66,82,30,76,19,31,172,103,46,
 139,24,255,4,227,157,204,156,51,115,102,206,231,239,220,5,170,94,129,137,75,194,216,98,94,103,117,115,121,76,131,177,125,89,125,82,123,60,243,58,142,242,204,185,243,188,118,156,
 227,155,203,238,238,195,251,205,229,71,92,28,169,190,184,84,143,113,137,53,244,103,181,237,87,253,113,137,233,48,12,198,165,181,104,139,25,84,253,155,114,74,191,0,54,138,163,
 12,62,131,207,129,23,217,34,91,31,128,65,246,163,175,213,8,147,213,107,35,203,94,108,3,111,40,171,83,24,15,165,177,222,116,97,23,188,140,206,150,42,102,181,87,78,86,182,170,134,
 215,241,121,26,243,252,2,76,115,217,139,222,206,173,136,132,81,61,35,185,39,113,23,46,199,76,178,54,151,183,224,0,40,189,28,149,182,58,131,79,152,30,76,34,98,234,162,216,133,141,
 102,39,170,40,192,101,53,201,146,191,37,77,44,177,209,74,211,5,206,187,5,6,216,47,53,96,123,22,50,103,251,192,84,17,74,227,185,56,106,51,91,161,96,182,163,48,171,141,139,65,152,
 66,66,11,102,43,158,75,36,80,147,184,147,139,112,17,235,216,103,111,239,245,92,10,175,194,40,44,58,125,5,59,112,50,103,245,4,78,192,5,156,194,51,60,191,134,75,110,173,237,46,192,
 121,156,192,115,184,218,120,67,63,115,46,11,102,10,97,232,50,235,114,182,148,118,178,41,188,12,135,77,202,124,12,96,238,35,161,234,189,129,23,249,212,139,230,25,53,48,205,52,93,
 163,117,53,154,170,81,85,163,178,70,69,66,167,241,14,46,241,1,226,136,152,179,197,59,184,148,254,49,132,48,15,176,137,192,76,131,196,105,104,162,86,81,160,165,255,26,173,162,137,
 86,145,210,183,192,55,175,194,211,60,91,120,230,184,174,27,41,131,155,40,224,29,87,179,232,16,55,55,7,165,147,81,23,165,49,101,54,224,75,180,81,108,18,29,226,69,225,110,175,224,
 42,212,25,47,130,193,110,234,192,215,252,56,74,162,24,46,251,174,54,106,68,245,14,9,155,160,22,120,207,104,240,29,90,178,140,28,24,220,47,166,112,61,251,208,192,111,56,239,238,
 93,255,251,62,99,32,193,75,61,190,235,123,229,110,218,194,85,79,225,59,98,20,238,227,235,220,11,221,149,25,180,116,194,159,111,96,192,24,213,59,139,179,156,215,69,230,19,24,35,
 135,117,206,171,206,162,67,129,234,61,235,11,104,103,84,64,223,167,254,40,163,101,92,84,43,150,46,249,219,205,7,116,11,91,104,61,57,75,223,8,48,25,28,119,252,222,113,49,86,249,
 74,180,211,156,181,61,215,168,157,7,251,199,150,242,250,91,58,132,94,121,7,53,151,139,98,6,165,153,69,214,32,110,211,100,101,31,89,45,81,98,23,205,205,197,209,109,186,198,35,
 141,191,249,25,60,132,223,153,251,98,20,239,146,139,20,217,250,41,250,137,58,177,90,57,79,51,108,233,20,253,194,187,49,222,205,114,141,96,48,175,219,107,54,111,138,22,154,103,
 108,79,58,252,179,178,79,164,195,2,153,36,39,170,199,201,167,197,85,106,8,59,177,81,46,56,2,230,75,114,17,55,112,188,65,208,137,77,114,10,115,55,58,208,197,173,122,87,6,140,
 110,42,208,124,163,70,108,241,104,18,245,98,214,187,134,53,42,221,22,182,133,211,116,148,177,194,209,192,85,90,199,58,55,203,2,229,19,137,187,161,228,154,112,203,145,125,244,
 188,220,118,228,41,201,181,41,195,144,215,183,51,80,250,21,217,16,217,200,235,109,227,188,122,218,142,60,170,224,112,240,184,130,229,224,113,5,223,148,163,80,165,183,130,187,
 132,116,64,238,161,85,220,115,139,205,98,227,244,29,102,125,7,37,243,123,223,11,26,92,63,243,116,61,191,138,123,244,160,84,186,74,31,5,174,247,119,135,199,248,253,135,242,97,
 102,145,190,144,14,85,238,221,231,193,158,48,205,25,120,248,15,220,29,158,9,70,185,30,103,229,33,254,23,237,160,172,62,193,90,222,224,232,14,200,56,90,104,142,227,120,110,6,
 21,211,203,65,150,99,151,220,247,87,164,50,159,49,239,234,58,142,0,109,108,123,18,79,227,36,100,248,222,205,96,127,120,26,171,228,69,63,36,17,252,200,17,116,242,187,227,88,143,
 247,2,75,191,6,130,59,188,11,55,240,31,243,122,152,226,183,207,154,73,188,39,219,43,105,222,87,41,143,141,140,175,73,112,184,252,61,184,16,90,250,35,168,82,119,176,57,116,94,
 200,150,22,190,179,44,104,12,235,84,149,102,252,89,154,193,99,228,106,242,125,248,64,194,255,223,127,242,83,11,255,2,70,214,226,128,0,0
};
#endif

//---------------------------
static esp_err_t eve_select()
{
    if (ft8_full_cs) return spi_device_select(eve_spi, 0);
    else {
        gpio_set_level(eve_spi->cs, 0);
        return ESP_OK;
    }
}

//-----------------------------
static esp_err_t eve_deselect()
{
    if (ft8_full_cs) return spi_device_deselect(eve_spi);
    else {
        gpio_set_level(eve_spi->cs, 1);
        return ESP_OK;
    }
}

#ifdef CONFIG_EVE_CHIP_TYPE1

//-----------------------------------------------
static int FT8_Fifo_write_file_block(int blksize)
{
    int bytesread, write_parc, write_remain, size, total=0;
    //uint32_t old_wp = ft8_stFifo.fifo_wp;

    // repeat sending in maximum file buffer sizes
    while (blksize > 0) {
        size = (blksize > ft8_stFifo.fbuff_size) ? ft8_stFifo.fbuff_size : blksize;
        // read block from file
        bytesread = fread(ft8_stFifo.g_scratch, 1, size, ft8_stFifo.pFile);
        if (bytesread <= 0) {
            ft8_stFifo.file_remain = 0;
            break;
        }
        total += bytesread;

        if ((ft8_stFifo.fifo_wp+bytesread) <= ft8_stFifo.fifo_len) {
            // the whole block fits into mediafifo buffer
            FT8_memWrite_flash_buffer(ft8_stFifo.fifo_buff + ft8_stFifo.fifo_wp, ft8_stFifo.g_scratch, bytesread, true);
            // adjust the write pointer
            ft8_stFifo.fifo_wp += bytesread;
            if (ft8_stFifo.fifo_wp >= ft8_stFifo.fifo_len) ft8_stFifo.fifo_wp = 0; // wrap to the buffer start
        }
        else {
            // wp will wrap the mediafifo buffer, write in two parts
            write_parc = ft8_stFifo.fifo_len - ft8_stFifo.fifo_wp; // until the end of buffer
            FT8_memWrite_flash_buffer(ft8_stFifo.fifo_buff + ft8_stFifo.fifo_wp, ft8_stFifo.g_scratch, write_parc, true);
            ft8_stFifo.fifo_wp = 0;
            write_remain = bytesread-write_parc; // from buffer start write remaining
            FT8_memWrite_flash_buffer(ft8_stFifo.fifo_buff + ft8_stFifo.fifo_wp, ft8_stFifo.g_scratch+write_parc, write_remain, true);
            // adjust the write pointer
            ft8_stFifo.fifo_wp += write_remain;
        }

        // adjust remaining file size
        if (bytesread < size) {
            ft8_stFifo.file_remain = 0; // the whole file written
            blksize = 0;
        }
        else {
            ft8_stFifo.file_remain -= bytesread;
            blksize -= size;
        }
    }

    //ESP_LOGD(TAG, "Write block (%d) at wp=%d (new wp=%d), remaining_in_file=%d", total, old_wp, ft8_stFifo.fifo_wp, ft8_stFifo.file_remain);
    return total;
}

//--------------------
void FT8_Fifo_deinit()
{
    if (ft8_stFifo.g_scratch) free(ft8_stFifo.g_scratch);
    memset((void *)&ft8_stFifo, 0, sizeof(FT8_Fifo_t));
}

//----------------------------
int FT8_Fifo_init(FILE *fhndl)
{
    int err = 0;
    ESP_LOGD(TAG, "=== MEDIAFIFO Init");
    // check if there is enough space in EVE RAM_G
    // minimum of 16KB is required
    ft8_stFifo.fifo_len = FT8_RAM_G_SIZE - ft8_ramg_ptr;
    if (ft8_stFifo.fifo_len < 16348) { err=-1; goto errexit; }

    ft8_stFifo.fifo_len = (ft8_stFifo.fifo_len / 16384) * 16384;
    if (ft8_stFifo.fifo_len > 65536) ft8_stFifo.fifo_len = 65536;

    // set the buffer start address
    ft8_stFifo.fifo_buff = FT8_RAM_G_SIZE-ft8_stFifo.fifo_len;

    ft8_stFifo.fifo_rp = 0;
    ft8_stFifo.fifo_wp = 0;
    ft8_stFifo.max_free = 0;
    ft8_stFifo.min_free = ft8_stFifo.fifo_len;

    // get file size
    ft8_stFifo.pFile = fhndl;
    if (fseek(ft8_stFifo.pFile,0,SEEK_END)) { err=-2; goto errexit; }
    ft8_stFifo.file_size = ftell(ft8_stFifo.pFile);
    if (ft8_stFifo.file_size < 0) return -2;
    if (fseek(ft8_stFifo.pFile,0,SEEK_SET)) { err=-2; goto errexit; }
    if (ft8_stFifo.file_size == 0) { err=-2; goto errexit; }
    ft8_stFifo.file_remain = ft8_stFifo.file_size;

    // Allocate file read buffer of minimum 512 bytes
    ft8_stFifo.fbuff_size = 8192;
    ft8_stFifo.g_scratch = NULL;
    while (ft8_stFifo.g_scratch == NULL){
        ft8_stFifo.fbuff_size /= 2;
        if (ft8_stFifo.fbuff_size < 512) break;
        ft8_stFifo.g_scratch = malloc(ft8_stFifo.fbuff_size);
    }
    if (ft8_stFifo.g_scratch == NULL) { err=-3; goto errexit; }
    ESP_LOGD(TAG, "    mediafifo buffer at %d, size=%d, file_buff_size=%d", ft8_stFifo.fifo_buff, ft8_stFifo.fifo_len, ft8_stFifo.fbuff_size);

    // === Fill mediafifo buffer from the file ===
    FT8_Fifo_write_file_block(ft8_stFifo.fifo_len - ft8_stFifo.fbuff_size);
    ESP_LOGD(TAG, "    buffer_filled=%d, remaining_in_file: %d", ft8_stFifo.fifo_wp, ft8_stFifo.file_remain);

    // Execute CMD_MEDIAFIFO, set mediafifo parameters
    FT8_start_cmd(CMD_MEDIAFIFO);
    FT8_send_long(ft8_stFifo.fifo_buff, ft8_stFifo.fifo_len, 0, 2);

    if (!FT8_cmd_execute(250)) err = -8;
    else return 0;

errexit:
    ESP_LOGE(TAG, "    Fifo init: error %d", err);
    FT8_Fifo_deinit();
    return err;
}

//--------------------
int FT8_Fifo_service()
{
    if (ft8_stFifo.file_remain <= 0) {
        // === Complete file was sent to mediafifo, wait until processed ===
        ESP_LOGD(TAG, "Service: file completed");
        ft8_stFifo.fifo_rp = FT8_memRead32(REG_MEDIAFIFO_READ); // get the read pointer

        uint64_t tmo = mp_hal_ticks_ms();
        uint8_t res = FT8_busy();
        while (res == 1) {
            mp_hal_reset_wdt();
            res = FT8_busy();
            if ((res == 1) && ((mp_hal_ticks_ms()-tmo) > 250)) res = 2;
        }

        // wait until all data are precessed
        while (ft8_stFifo.fifo_rp < ft8_stFifo.fifo_wp) {
            ft8_stFifo.fifo_rp = FT8_memRead32(REG_MEDIAFIFO_READ);
            res++;
            if ((mp_hal_ticks_ms()-tmo) > 100) {
                ESP_LOGW(TAG, "Mediafifo processing timeout (ft8_stFifo.fifo_rp=%d < wp=%d)", ft8_stFifo.fifo_rp, ft8_stFifo.fifo_wp);
                if (cmd_burst) eve_select();
                return -1;
            }
        }
        ESP_LOGD(TAG, "Service: finished");
        if (cmd_burst) eve_select();
        return 0;
    }

    // === Still some data to send ===
    uint32_t fifo_free, written=0;

    // Get the read pointer
    ft8_stFifo.fifo_rp = FT8_memRead32(REG_MEDIAFIFO_READ);
    // and calculate the free fifo space
    if (ft8_stFifo.fifo_rp < ft8_stFifo.fifo_wp) fifo_free = (ft8_stFifo.fifo_len - ft8_stFifo.fifo_wp) + ft8_stFifo.fifo_rp;
    else fifo_free = ft8_stFifo.fifo_rp - ft8_stFifo.fifo_wp;
    // keep minimum of 256 free buffer
    if (fifo_free >= MIN_FIFO_FREE) fifo_free -= MIN_FIFO_FREE;
    if (fifo_free > ft8_stFifo.max_free) ft8_stFifo.max_free = fifo_free;
    if (fifo_free < ft8_stFifo.min_free) ft8_stFifo.min_free = fifo_free;

    if (fifo_free >= MIN_FIFO_FREE) {
        // send file block
        written = FT8_Fifo_write_file_block(fifo_free);
        if (written > 0) FT8_memWrite32(REG_MEDIAFIFO_WRITE, ft8_stFifo.fifo_wp);
    }

    //if (written > 0) {
    //    ESP_LOGD(TAG, "Service: free=%d, written: %d, rd=%d, wp=%d", fifo_free, written, ft8_stFifo.fifo_rp, ft8_stFifo.fifo_wp);
    //}
    //if (cmd_burst) eve_select();
    return 1;
}

//-------------------------------------------------------------------------------------
int FT8_sendDataViaMediafifo(FILE *pFile, uint32_t ptr, uint32_t options, uint8_t type)
{
    if (type == 0) {
        ESP_LOGD(TAG, "=== Load image");
        FT8_start_cmd(CMD_LOADIMAGE);
        FT8_send_long(ptr, options, 0, 2);
        eve_deselect();
        FT8_cmd_start();
    }
    else if (type == 1) {
        ESP_LOGD(TAG, "==== Play video");
        FT8_start_cmd(CMD_PLAYVIDEO);
        FT8_send_long(options, 0, 0, 1);
        eve_deselect();
        FT8_cmd_start();
    }
    else if (type == 2) {
        ESP_LOGD(TAG, "=== Start video");

        FT8_cmd_dl(CMD_VIDEOSTART);
        //FT8_send_long(options, 0, 0, 1);
        FT8_cmd_execute(250);
        ESP_LOGD(TAG, "    VIDEOSTART executed");
    }
    else {
        ESP_LOGE(TAG, "=== Unknown type");
        return -9;
    }

    // update the read and write pointers of mediafifo
    FT8_memWrite32(REG_MEDIAFIFO_READ, 0);
    FT8_memWrite32(REG_MEDIAFIFO_WRITE, ft8_stFifo.fifo_wp);

    if (type < 2) {
        int res, rdptr=0;
        uint64_t tmo = mp_hal_ticks_ms();
        mp_hal_set_wdt_tmo();
        // Execute the service to send the whole file
        do {
            res = FT8_Fifo_service();
            if (res < 0) return -9;
            mp_hal_reset_wdt();
            if (rdptr == ft8_stFifo.fifo_rp) {
                if ((mp_hal_ticks_ms()-tmo) > 250) {
                    // read pointer does not advance
                    ESP_LOGE(TAG, "Mediafifo processing timeout (rdptr=%d < wp=%d)", rdptr, ft8_stFifo.fifo_wp);
                    FT8_CP_reset();
                    return -10;
                }
            }
            else tmo = mp_hal_ticks_ms();
            rdptr = ft8_stFifo.fifo_rp;
        } while (res);

        tmo = mp_hal_ticks_ms();
        res = FT8_busy();
        while (res == 1) {
            if ((mp_hal_ticks_ms()-tmo) > 250) {
                res = 2;
                break;
            }
            mp_hal_reset_wdt();
            res = FT8_busy();
        }
        if (res != 0) {
            ESP_LOGE(TAG, "Error executing");
            FT8_CP_reset();
        }

        ESP_LOGD(TAG, "Finished");
        return ft8_stFifo.file_remain;
    }

    return 0;
}
#endif

// ==== Basic SPI commands ====

// Send command to FT8xx, only used in Init function
//-----------------------------
void FT8_cmdWrite(uint8_t data)
{
	if (eve_select()) return;
	eve_spi->handle->host->hw->data_buf[0] = (uint32_t)data;
    _spi_transfer_start(eve_spi, 24, 0);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    eve_deselect();
}

// ==== Memory read/write ========================================================================

// Prepare 32-bit address to be sent to FT8xx
//----------------------------------------------------
uint32_t FT8_address(uint32_t ftAddress, uint8_t mask)
{
	uint32_t wd = ((ftAddress&0x00FF0000) >> 16) | mask;    // Memory Write plus high address byte
	wd |= (ftAddress & 0x0000FF00);			                // middle address byte
	wd |= ((ftAddress & 0xFF) << 16);                       // low address byte & dummy byte
	return wd;
}

//--------------------------------------
uint8_t FT8_memRead8(uint32_t ftAddress)
{
	uint8_t ftData8 = 0;

	if (eve_select()) return 0;
	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_READ);
	eve_spi->handle->host->hw->data_buf[1] = 0;
    _spi_transfer_start(eve_spi, 40, 40);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

    ftData8 = (uint8_t)eve_spi->handle->host->hw->data_buf[1];
    eve_deselect();

    return ftData8;	// return byte read
}

//----------------------------------------
uint16_t FT8_memRead16(uint32_t ftAddress)
{
	uint16_t ftData16 = 0;

	if (eve_select()) return 0;
	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_READ);
	eve_spi->handle->host->hw->data_buf[1] = 0;
    _spi_transfer_start(eve_spi, 48, 48);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

    ftData16 = (uint16_t)eve_spi->handle->host->hw->data_buf[1];
    eve_deselect();

	return ftData16;	// return 16-bit integer read
}

//----------------------------------------
uint32_t FT8_memRead32(uint32_t ftAddress)
{
	uint32_t ftData32= 0;

	if (eve_select()) return 0;
	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_READ);
	eve_spi->handle->host->hw->data_buf[1] = 0;
    _spi_transfer_start(eve_spi, 64, 64);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

    ftData32 = eve_spi->handle->host->hw->data_buf[1];
    eve_deselect();

	return ftData32;	// return 32-bit long read
}

//----------------------------------------------------------------------
void FT8_memRead_buffer(uint32_t ftAddress, uint8_t *data, uint16_t len)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction
    memset(data, 0, len);

    if (eve_select()) return;
    eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_READ);
    _spi_transfer_start(eve_spi, 32, 32);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

    t.length = len * 8;
    t.tx_buffer = data;
    t.rxlength = len * 8;
    t.rx_buffer = data;

    spi_transfer_data_nodma(eve_spi, &t); // Send using direct mode
    eve_deselect();
}

//-----------------------------------------------------
void FT8_memWrite8(uint32_t ftAddress, uint8_t ftData8)
{
	if (eve_select()) return;
	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE) | (uint32_t)((uint32_t)ftData8 << 24);
    _spi_transfer_start(eve_spi, 32, 32);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    eve_deselect();
}

//--------------------------------------------------------
void FT8_memWrite16(uint32_t ftAddress, uint16_t ftData16)
{
	if (eve_select()) return;
	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE) | (uint32_t)((uint32_t)ftData16 << 24);
	eve_spi->handle->host->hw->data_buf[1] = (uint32_t)(ftData16 >> 8); // high byte
    _spi_transfer_start(eve_spi, 40, 40);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    eve_deselect();
}

//--------------------------------------------------------
void FT8_memWrite32(uint32_t ftAddress, uint32_t ftData32)
{
	if (eve_select()) return;
	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE) | (ftData32 << 24);
	eve_spi->handle->host->hw->data_buf[1] = (ftData32 >> 8);
    _spi_transfer_start(eve_spi, 56, 56);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    eve_deselect();
}

//-----------------------------------------------------------------------
static int FT8_send_data(uint8_t *data, int data_len, bool check_padding)
{
    uint8_t padding = 0;
    if (check_padding) {
        // ensure 4-byte alignment
        padding = data_len % 4;
        if (padding) padding = 4 - padding;
    }

    if ((data_len + padding) <= 64) {
        // === Send length <= 64, send directly ===
        memset((uint8_t *)eve_spi->handle->host->hw->data_buf, 0, 64);
        int bits = 0;
        int idx = 0;
        uint32_t wd = 0;
        for (int i=0; i<data_len; i++) {
            wd |= data[i] << bits;
            bits += 8;
            if (bits == 32) {
                eve_spi->handle->host->hw->data_buf[idx] = wd;
                bits = 0;
                idx++;
                wd = 0;
            }
        }
        if (bits) eve_spi->handle->host->hw->data_buf[idx] = wd;
        _spi_transfer_start(eve_spi, (data_len + padding) * 8, 0);
        while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    }
    else {
        // === Send length > 64, send using block transfer function ===
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));  //Zero out the transaction

        t.length = data_len * 8;
        t.tx_buffer = data;
        t.rxlength = 0;
        t.rx_buffer = NULL;

        spi_transfer_data_nodma(eve_spi, &t); // Send using direct mode

        if (padding) {
            // send padding bytes
            eve_spi->handle->host->hw->data_buf[0] = 0;
            _spi_transfer_start(eve_spi, padding * 8, 0);
            while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
        }
    }
    return (data_len + padding);
}

//------------------------------------------------------------------------------------------------------
int FT8_memWrite_flash_buffer(uint32_t ftAddress, const uint8_t *data, uint16_t len, bool check_padding)
{
	if (eve_select()) return 0;
	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
    _spi_transfer_start(eve_spi, 24, 24);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

	int res = FT8_send_data((uint8_t *)data, len, check_padding);

    eve_deselect();
    return res;
}

//-----------------
void FT8_CP_reset()
{
    FT8_memWrite8(REG_CPURESET, 1);     // hold co-processor engine in the reset condition
    FT8_memWrite16(REG_CMD_READ, 0);    // set REG_CMD_READ to 0
    FT8_memWrite16(REG_CMD_WRITE, 0);   // set REG_CMD_WRITE to 0
    eve_cmdOffset = 0;                  // reset eve_cmdOffset
    FT8_memWrite8(REG_CPURESET, 0);     // set REG_CMD_WRITE to 0 to restart the co-processor engine
}

// Check if the graphics processor completed executing the current command list.
// This is the case when REG_CMD_READ matches REG_CMD_WRITE (eve_cmdOffset),
// indicating that all commands have been executed.
//--------------------
uint8_t FT8_busy(void)
{
	uint16_t cmdBufferRead, cmdBufferWrite;

	cmdBufferRead = FT8_memRead16(REG_CMD_READ);	// read the graphics processor read pointer
    cmdBufferWrite = FT8_memRead16(REG_CMD_WRITE);  // read the graphics processor write pointer

    if (cmdBufferRead == 0xFFF) {
        // EVE co-processor engine fault
        FT8_CP_reset();
        ESP_LOGE(TAG, "EVE co-processor fault");
        return 2;
    }

    if (cmdBufferWrite != cmdBufferRead) return 1;
    else return 0;
}

//------------------------------
uint32_t FT8_get_touch_tag(void)
{
	uint32_t value;

	value = FT8_memRead32(REG_TOUCH_TAG);
	return (value & 0xFF);
}


// Order the command coprocessor to start processing its FIFO queue
// do not wait for completion !
//----------------------
void FT8_cmd_start(void)
{
	uint32_t ftAddress = REG_CMD_WRITE;
	FT8_memWrite16(ftAddress, eve_cmdOffset);
}


// Order the command coprocessor to start processing its FIFO queue
// Wait for completion !
//------------------------------
bool FT8_cmd_execute(int tmo_ms)
{
	FT8_cmd_start();
    uint64_t tmo = mp_hal_ticks_ms();
	uint8_t res = FT8_busy();
    while (res == 1) {
        mp_hal_reset_wdt();
        res = FT8_busy();
        if ((res == 1) && ((mp_hal_ticks_ms()-tmo) > tmo_ms)) {
            FT8_CP_reset();
            res = 2;
        }
    }
	return (res == 0);
}

//--------------------------
void FT8_get_cmdoffset(void)
{
	eve_cmdOffset = FT8_memRead16(REG_CMD_WRITE);
}

// make current value of cmdOffset available while limiting access to that var to the FT8_commands module
//---------------------------------
uint16_t FT8_report_cmdoffset(void)
{
    return (eve_cmdOffset);
}


//----------------------------------------
void FT8_inc_cmdoffset(uint16_t increment)
{
	eve_cmdOffset += increment;
	eve_cmdOffset &= 0x0fff;
}


// ==== Write to EVE command FIFO ============================================================================
/*
These eliminate the overhead of transmitting the command fifo address with every single command,
just wrap a sequence of commands with these and the address is only transmitted once at the start of the block.
Be careful to not use any functions in the sequence that do not address the command-fifo,
as for example any FT8_mem...() function.
*/
//----------------------------
void FT8_start_cmd_burst(void)
{
	uint32_t ftAddress;
	
	cmd_burst = 42;
	ftAddress = FT8_RAM_CMD + eve_cmdOffset;
	if (eve_select()) return;

	eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
    _spi_transfer_start(eve_spi, 24, 24);
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
}

//--------------------------
void FT8_end_cmd_burst(void)
{
	cmd_burst = 0;
    eve_deselect();
}

// Write a string to coprocessor memory in context of a command
// no chip-select, just plain spi-transfers
//-------------------------------------
void FT8_write_string(const char *text)
{
    size_t textlen = strlen(text);
    // sent string must be terminated with null character (ASCII 0)
    int res = FT8_send_data((uint8_t *)text, textlen+1, true);

    FT8_inc_cmdoffset(res);
}

// Begin a coprocessor command
//----------------------------------
void FT8_start_cmd(uint32_t command)
{
	uint32_t ftAddress;
	
    if (eve_select()) return;

    if (cmd_burst == 0)	{
		ftAddress = FT8_RAM_CMD + eve_cmdOffset;
		eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
	    _spi_transfer_start(eve_spi, 24, 24);       // send address
	    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready
	}

	eve_spi->handle->host->hw->data_buf[0] = command;
    _spi_transfer_start(eve_spi, 32, 32);
    while (eve_spi->handle->host->hw->cmd.usr);     // Wait for SPI bus ready

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
	if (cmd_burst == 0) eve_deselect();
}

// ============================================
// ==== Commands to draw graphics objects: ====
// ============================================

// Send number of 16-bit parameters
// 'len' can have the value 2, 4, 6 or 8
//-------------------------------------------------------------------------------------------------------------------------------------------
static void FT8_send_params(int16_t p1, int16_t p2, int16_t p3, uint16_t p4, uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8, uint8_t len)
{
	eve_spi->handle->host->hw->data_buf[0] = ((uint32_t)p1 | (uint32_t)p2 << 16);
	if (len < 4) goto send;

    eve_spi->handle->host->hw->data_buf[1] = ((uint32_t)p3 | (uint32_t)p4 << 16);
	if (len < 6) goto send;

    eve_spi->handle->host->hw->data_buf[2] = ((uint32_t)p5 | (uint32_t)p6 << 16);
	if (len < 8) goto send;

    eve_spi->handle->host->hw->data_buf[3] = ((uint32_t)p7 | (uint32_t)p8 << 16);

send:
    _spi_transfer_start(eve_spi, len*16, len*16); // send
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

	FT8_inc_cmdoffset(len * 2);
}

// Send number of 32-bit longs
//--------------------------------------------------------------------------
void FT8_send_long(uint32_t val1, uint32_t val2, uint32_t val3, uint8_t len)
{
	eve_spi->handle->host->hw->data_buf[0] = val1;
	if (len < 2) goto send;

	eve_spi->handle->host->hw->data_buf[1] = val2;
	if (len < 3) goto send;

	eve_spi->handle->host->hw->data_buf[2] = val3;

send:
    _spi_transfer_start(eve_spi, len*32, len*32); // send
    while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

	FT8_inc_cmdoffset(len * 4);
}

// Draw text
//-----------------------------------------------------------------------------------------
void FT8_cmd_text(int16_t x0, int16_t y0, int16_t font, uint16_t options, const char* text)
{
	FT8_start_cmd(CMD_TEXT);
	FT8_send_params(x0, y0, font, options, 0, 0, 0, 0, 4);
	FT8_write_string(text);

	if (cmd_burst == 0) eve_deselect();
}

// Draw a button with text
//-------------------------------------------------------------------------------------------------------------------
void FT8_cmd_button(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t font, uint16_t options, const char* text)
{
	FT8_start_cmd(CMD_BUTTON);
	FT8_send_params(x0, y0, w0, h0, font, options, 0, 0, 6);
	FT8_write_string(text);

	if (cmd_burst == 0) eve_deselect();
}

// Draw a clock
//----------------------------------------------------------------------------------------------------------------------------------------------
void FT8_cmd_clock(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t hours, uint16_t minutes, uint16_t seconds, uint16_t millisecs)
{
	FT8_start_cmd(CMD_CLOCK);
	FT8_send_params(x0, y0, r0, options, hours, minutes, seconds, millisecs, 8);

	if (cmd_burst == 0) eve_deselect();
}

//----------------------------------
void FT8_cmd_bgcolor(uint32_t color)
{
	FT8_start_cmd(CMD_BGCOLOR);
	FT8_send_long(color, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//----------------------------------
void FT8_cmd_fgcolor(uint32_t color)
{
	FT8_start_cmd(CMD_FGCOLOR);
	FT8_send_long(color, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//------------------------------------
void FT8_cmd_gradcolor(uint32_t color)
{
	FT8_start_cmd(CMD_GRADCOLOR);
	FT8_send_long(color, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//------------------------------------------------------------------------------------------------------------------------------------
void FT8_cmd_gauge(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t major, uint16_t minor, uint16_t val, uint16_t range)
{
	FT8_start_cmd(CMD_GAUGE);
	FT8_send_params(x0, y0, r0, options, major, minor, val, range, 8);

	if (cmd_burst == 0) eve_deselect();
}

//-------------------------------------------------------------------------------------------------
void FT8_cmd_gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1)
{
	FT8_start_cmd(CMD_GRADIENT);
	FT8_send_params(x0, y0, 0, 0, 0, 0, 0, 0, 2);
    FT8_send_long(rgb0, 0, 0, 1);
    FT8_send_params(x1, y1, 0, 0, 0, 0, 0, 0, 2);
    FT8_send_long(rgb1, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//-----------------------------------------------------------------------------------------------------------------
void FT8_cmd_keys(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t font, uint16_t options, const char* text)
{
	FT8_start_cmd(CMD_KEYS);

	FT8_send_params(x0, y0, w0, h0, font, options, 0, 0, 6);
	FT8_write_string(text);

	if (cmd_burst == 0) eve_deselect();
}

//-------------------------------------------------------------------------------------------------------------------
void FT8_cmd_progress(int16_t x0, int16_t y0, int16_t w0, int16_t h0, uint16_t options, uint16_t val, uint16_t range)
{
	FT8_start_cmd(CMD_PROGRESS);
	FT8_send_params(x0, y0, w0, h0, options, val, range, 0, 8);

	if (cmd_burst == 0) eve_deselect();
}

//-----------------------------------------------------------------------------------------------------------------------------------
void FT8_cmd_scrollbar(int16_t x0, int16_t y0, int16_t w0, int16_t h0, uint16_t options, uint16_t val, uint16_t size, uint16_t range)
{
	FT8_start_cmd(CMD_SCROLLBAR);
	FT8_send_params(x0, y0, w0, h0, options, val, size, range, 8);

	if (cmd_burst == 0) eve_deselect();
}

//-----------------------------------------------------------------------------------------------------------------
void FT8_cmd_slider(int16_t x1, int16_t y1, int16_t w1, int16_t h1, uint16_t options, uint16_t val, uint16_t range)
{
	FT8_start_cmd(CMD_SLIDER);
	FT8_send_params(x1, y1, w1, h1, options, val, range, 0, 8);

	if (cmd_burst == 0) eve_deselect();
}

//-----------------------------------------------------------------------------------
void FT8_cmd_dial(int16_t x0, int16_t y0, int16_t r0, uint16_t options, uint16_t val)
{
	FT8_start_cmd(CMD_DIAL);
	FT8_send_params(x0, y0, r0, options, val, 0, 0, 0, 6);

	if (cmd_burst == 0) eve_deselect();
}

//-----------------------------------------------------------------------------------------------------------------------
void FT8_cmd_toggle(int16_t x0, int16_t y0, int16_t w0, int16_t font, uint16_t options, uint16_t state, const char* text)
{
	FT8_start_cmd(CMD_TOGGLE);
	FT8_send_params(x0, y0, w0, font, options, state, 0, 0, 6);
	FT8_write_string(text);

	if (cmd_burst == 0) eve_deselect();
}

#ifdef CONFIG_EVE_CHIP_TYPE1
//---------------------------------
void FT8_cmd_setbase(uint32_t base)
{
	FT8_start_cmd(CMD_SETBASE);
    FT8_send_long(base, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//----------------------------------------------------------------------------------
void FT8_cmd_setbitmap(uint32_t addr, uint16_t fmt, uint16_t width, uint16_t height)
{
	FT8_start_cmd(CMD_SETBITMAP);
    FT8_send_long(addr, 0, 0, 1);
	FT8_send_params(fmt, width, height, 0, 0, 0, 0, 0, 4);

	if (cmd_burst == 0) eve_deselect();
}
#endif

//-------------------------------------------
void FT8_cmd_bitmapXY(uint16_t x, uint16_t y)
{
    FT8_cmd_dl(DL_BEGIN | FT8_BITMAPS);
    FT8_cmd_dl(VERTEX2F(x*16, y*16));
    FT8_cmd_dl(DL_END);
}

//-----------------------------------------------------------------------------------------
void FT8_cmd_number(int16_t x0, int16_t y0, int16_t font, uint16_t options, int32_t number)
{
	FT8_start_cmd(CMD_NUMBER);
	FT8_send_params(x0, y0, font, options, 0, 0, 0, 0, 4);
    FT8_send_long(number, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

// ========================================
// ==== Commands to operate on memory: ====
// ========================================

//----------------------------------------------
void FT8_cmd_memzero(uint32_t ptr, uint32_t num)
{
	FT8_start_cmd(CMD_MEMZERO);
	FT8_send_long(ptr, num, 0, 2);

	if (cmd_burst == 0) eve_deselect();
}

//------------------------------------------------------------
void FT8_cmd_memset(uint32_t ptr, uint8_t value, uint32_t num)
{
	FT8_start_cmd(CMD_MEMSET);
	FT8_send_long(ptr, (uint32_t)value, num, 3);

	if (cmd_burst == 0) eve_deselect();
}

// Write bytes into memory
// If the number of bytes is not a multiple of 4, then 1, 2 or 3 bytes should be
// appended to ensure 4-byte alignment of the next command
//---------------------------------------------------------------------
void FT8_cmd_memwrite(uint32_t dest, const uint8_t *data, uint32_t num)
{
	FT8_start_cmd(CMD_MEMWRITE);

    FT8_send_long(dest, num, 0, 2);
    int res = FT8_send_data((uint8_t *)data, num, true);
    FT8_inc_cmdoffset(res);

    if (cmd_burst == 0) eve_deselect();
}

//------------------------------------------------------------
void FT8_cmd_memcpy(uint32_t dest, uint32_t src, uint32_t num)
{
	FT8_start_cmd(CMD_MEMCPY);
	FT8_send_long(dest, src, num, 3);

	if (cmd_burst == 0) eve_deselect();
}

//---------------------------------------------
void FT8_cmd_append(uint32_t ptr, uint32_t num)
{
	FT8_start_cmd(CMD_APPEND);
	FT8_send_long(ptr, num, 0, 2);

	if (cmd_burst == 0) eve_deselect();
}

// ============================================================
// ==== Commands for loading image data into FT8xx memory: ====
// ============================================================

// Decompress data into memory
// If the number of bytes is not a multiple of 4, then 1, 2 or 3 bytes should be
// appended to ensure 4-byte alignment of the next command
//-------------------------------------------------------------------
void FT8_cmd_inflate(uint32_t ptr, const uint8_t *data, uint16_t len)
{
	FT8_start_cmd(CMD_INFLATE);
	FT8_send_long(ptr, 0, 0, 1);

    int res = FT8_send_data((uint8_t *)data, len, true);
    FT8_inc_cmdoffset(res);

	if (cmd_burst == 0) eve_deselect();
}

/*
 * This is meant to be called outside display-list building,
 * it includes executing the command and waiting for completion, does not support cmd-burst
 */
//--------------------------------------------------------------------------------------------
int FT8_cmd_loadimage(uint32_t ptr, uint32_t options, FILE *fhndl, uint32_t len, uint8_t type)
{
	int bytes_left, bytes_sent=0, block_len, res;
	uint32_t ftAddress;
	uint8_t * buff = NULL;

	if (type == 1) {
        FT8_start_cmd(CMD_LOADIMAGE);
        FT8_send_long(ptr, options, 0, 2);
        eve_deselect();
	}
	else if (type == 2) {
        FT8_start_cmd(CMD_INFLATE);
        FT8_send_long(ptr, 0, 0, 1);
        eve_deselect();
	}

    if (fhndl == NULL) return -2;
    buff = malloc(1024);
    if (buff == NULL) return -3;

    bytes_sent = 0;
    bytes_left = len;

    while (bytes_left > 0) {
        // read block from file
        block_len = (bytes_left > 1024) ? 1024 : bytes_left;
        block_len = fread(buff, 1, block_len, fhndl);
        if (block_len <= 0) {
            ESP_LOGE(TAG, "File read error [%d]: sent=%d, remaining=%d", block_len, bytes_sent, bytes_left);
            break;
        }

        if (type < 3) {
            // sending via command buffer
            ftAddress = FT8_RAM_CMD + eve_cmdOffset;
            eve_select();

            eve_spi->handle->host->hw->data_buf[0] = FT8_address(ftAddress, MEM_WRITE);
            _spi_transfer_start(eve_spi, 24, 24);       // send address
            while (eve_spi->handle->host->hw->cmd.usr); // Wait for SPI bus ready

            // Send data block (alligned to 4 bytes)
            res = FT8_send_data(buff, block_len, true);
            FT8_inc_cmdoffset(res);
            eve_deselect();

            bytes_left -= block_len;
            bytes_sent += res;
            if (!FT8_cmd_execute(250)) {
                free(buff);
                ESP_LOGE(TAG, "Execute error: sent=%d, remaining=%d", bytes_sent, bytes_left);
                return (bytes_sent * -1);
            }
        }
        else {
            // send directly to RAM_G
            res = FT8_memWrite_flash_buffer(ptr + bytes_sent, buff, block_len, true);

            bytes_left -= block_len;
            bytes_sent += res;
        }
    }
    free(buff);
	return bytes_sent;
}


#ifdef CONFIG_EVE_CHIP_TYPE1
// this is meant to be called outside display-list building,
// does not support cmd-burst
//-------------------------------------------------
void FT8_cmd_mediafifo(uint32_t ptr, uint32_t size)
{
	FT8_start_cmd(CMD_MEDIAFIFO);
	FT8_send_long(ptr, size, 0, 2);

	eve_deselect();
}

//------------------------------------
void FT8_cmd_videoframe(uint32_t addr)
{
    FT8_start_cmd(CMD_VIDEOFRAME);
    FT8_send_long(addr, ft8_stFifo.fifo_buff-4, 0, 2);

    if (cmd_burst == 0) eve_deselect();
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

	if (cmd_burst == 0) eve_deselect();
}

//----------------------------------------
void FT8_cmd_scale(int32_t sx, int32_t sy)
{
	FT8_start_cmd(CMD_SCALE);
	FT8_send_long(sx, sy, 0, 2);

	if (cmd_burst == 0) eve_deselect();
}

//------------------------------
void FT8_cmd_rotate(int32_t ang)
{
	FT8_start_cmd(CMD_ROTATE);
	FT8_send_long(ang, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//----------------------
void FT8_cmd_setmatrix()
{
    FT8_start_cmd(CMD_SETMATRIX);

    if (cmd_burst == 0) eve_deselect();
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

	if (cmd_burst == 0) eve_deselect();
}


// =========================
// ==== Other commands: ====
// =========================

//--------------------------
void FT8_cmd_calibrate(void)
{
	FT8_start_cmd(CMD_CALIBRATE);
	FT8_send_long(0, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//---------------------------------
void FT8_cmd_interrupt(uint32_t ms)
{
	FT8_start_cmd(CMD_INTERRUPT);
	FT8_send_long(ms, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}


#ifdef CONFIG_EVE_CHIP_TYPE1
//---------------------------------------------------
void FT8_cmd_romfont(uint32_t font, uint32_t romslot)
{
	FT8_start_cmd(CMD_ROMFONT);
	FT8_send_long(font, romslot & 0xFFFF, 0, 2);

	if (cmd_burst == 0) eve_deselect();
}

//--------------------------------------------------------------------
void FT8_cmd_setfont2(uint32_t font, uint32_t ptr, uint32_t firstchar)
{
    FT8_start_cmd(CMD_SETFONT2);
    FT8_send_long(font, ptr, firstchar, 3);

    if (cmd_burst == 0) eve_deselect();
}

//--------------------------------
void FT8_cmd_setrotate(uint32_t r)
{
    FT8_start_cmd(CMD_SETROTATE);
    FT8_send_long(r, 0, 0, 1);

    if (cmd_burst == 0) eve_deselect();
}

//--------------------------------------
void FT8_cmd_setscratch(uint32_t handle)
{
    FT8_start_cmd(CMD_SETSCRATCH);
    FT8_send_long(handle, 0, 0, 1);

    if (cmd_burst == 0) eve_deselect();
}

//------------------------------------------------------------------------------------------------
void FT8_cmd_snapshot2(uint32_t fmt, uint32_t ptr, int16_t x0, int16_t y0, int16_t w0, int16_t h0)
{
    FT8_start_cmd(CMD_SNAPSHOT2);
    FT8_send_long(fmt, ptr, 0, 2);
    FT8_send_params(x0, y0, w0, h0, 0, 0, 0, 0, 4);

    if (cmd_burst == 0) eve_deselect();
}

#endif

//-----------------------------------------------
void FT8_cmd_setfont(uint32_t font, uint32_t ptr)
{
	FT8_start_cmd(CMD_SETFONT);
	FT8_send_long(font, ptr, 0, 2);

	if (cmd_burst == 0) eve_deselect();
}

//--------------------------------------------------------------------------------------------------
void FT8_cmd_sketch(int16_t x0, int16_t y0, uint16_t w0, uint16_t h0, uint32_t ptr, uint16_t format)
{
	FT8_start_cmd(CMD_SKETCH);
	FT8_send_params(x0, y0, w0, h0, 0, 0, 0, 0, 4);
    FT8_send_long(ptr, (uint32_t)format, 0, 2);

	if (cmd_burst == 0) eve_deselect();
}

//---------------------------------
void FT8_cmd_snapshot(uint32_t ptr)
{
	FT8_start_cmd(CMD_SNAPSHOT);
	FT8_send_long(ptr, 0, 0, 1);

	if (cmd_burst == 0) eve_deselect();
}

//--------------------------------------------------------------------------
void FT8_cmd_spinner(int16_t x0, int16_t y0, uint16_t style, uint16_t scale)
{
	FT8_start_cmd(CMD_SPINNER);
	FT8_send_params(x0, y0, style, scale, 0, 0, 0, 0, 4);

	if (cmd_burst == 0) eve_deselect();
}

//-----------------------------------------------------------------------------
void FT8_cmd_track(int16_t x0, int16_t y0, int16_t w0, int16_t h0, int16_t tag)
{
	FT8_start_cmd(CMD_TRACK);
	FT8_send_params(x0, y0, w0, h0, tag, 0, 0, 0, 6);

	if (cmd_burst == 0) eve_deselect();
}


// ====================================================================
// ==== Commands that return values by writing to the command-fifo ====
// ====================================================================

/*
 * This is handled by having this functions return the offset address on the command-fifo from
 * which the results can be fetched after execution: FT8_memRead32(FT8_RAM_CMD + offset)
 *
 * Note: these are different than the functions in the Programmers Guide from FTDI,
	this is because I have no idea why anyone would want to pass "result" as an actual argument to these functions
	when this only marks the offset the command-processor is writing to.
	It may even be okay to not transfer anything at all, just advance the offset by 4 bytes

 * Example of using FT8_cmd_memcrc:

   offset = FT8_cmd_memcrc(my_ptr_to_some_memory_region, some_amount_of_bytes);
   FT8_cmd_execute(250);
   crc32 = FT8_memRead32(FT8_RAM_CMD + offset);

*/

// Compute a CRC-32 for memory segment
// returns offset pointing to the CRC value
//-------------------------------------------------
uint16_t FT8_cmd_memcrc(uint32_t ptr, uint32_t num)
{
	uint16_t offset;

	FT8_start_cmd(CMD_MEMCRC);
	FT8_send_long(ptr, num, 0, 3);
	offset = eve_cmdOffset - 4;

	if (cmd_burst == 0) eve_deselect();

	return offset;
}

// Get the end memory address of data inflated by CMD_INFLATE
//---------------------------
uint16_t FT8_cmd_getptr(void)
{
	uint16_t offset;

	FT8_start_cmd(CMD_GETPTR);
	FT8_send_long(0, 0, 0, 1);
	offset = eve_cmdOffset - 4;

	if (cmd_burst == 0) eve_deselect();

	return offset;
}

// Read a register value, returns offset pointing to the register value
//------------------------------------
uint16_t FT8_cmd_regread(uint32_t ptr)
{
	uint16_t offset;

	FT8_start_cmd(CMD_REGREAD);
	FT8_send_long(ptr, 0, 0, 2);
	offset = eve_cmdOffset - 4;

	if (cmd_burst == 0) eve_deselect();

	return offset;
}

/*
 * Get the image properties decompressed by CMD_LOADIMAGE
 *
 * Be aware that this returns the first offset pointing to "width",
 * in order to also read "height" you need to execute:

   offset = FT8_cmd_getprops(my_last_picture_pointer);
   FT8_cmd_execute(250);
   width = FT8_memRead32(FT8_RAM_CMD + offset);
   offset += 4;
   offset &= 0x0fff;
   height = FT8_memRead32(FT8_RAM_CMD + offset);
*/
//-------------------------------------
uint16_t FT8_cmd_getprops(uint32_t ptr)
{
	uint16_t offset;

	FT8_start_cmd(CMD_GETPROPS);
	FT8_send_long(ptr, 0, 0, 3);
	offset = eve_cmdOffset - 8;

	if (cmd_burst == 0) eve_deselect();

	return offset;
}

// ===============================================================================
// ==== meta-commands, sequences of several display-list entries              ====
// ==== condensed into simpler to use functions at the price of some overhead ====
// ===============================================================================

//-------------------------------------------------------
void FT8_cmd_point(int16_t x0, int16_t y0, uint16_t size)
{
	uint32_t calc1, calc2;

	FT8_start_cmd((DL_BEGIN | FT8_POINTS));

	calc1 = POINT_SIZE(size*16);
	calc2 = VERTEX2F(x0 * 16, y0 * 16);
	FT8_send_long(calc1, calc2, DL_END, 3);

	if (cmd_burst == 0) eve_deselect();
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

	if (cmd_burst == 0) eve_deselect();
}

//-------------------------------------------------------------------------------
void FT8_cmd_strip(uint16_t *data, uint16_t length, uint8_t type, uint16_t width)
{
    if ((type < FT8_LINE_STRIP) || (type > FT8_EDGE_STRIP_B)) return;

    uint32_t calc, lwidth;

    FT8_start_cmd((DL_BEGIN | type));

    lwidth = LINE_WIDTH(width * 16);
    FT8_send_long(lwidth, 0, 0, 1);
    for (int i=0; i<length; i+=2) {
        calc = VERTEX2F(data[i] * 16, data[i+1] * 16);
        FT8_send_long(calc, 0, 0, 1);
    }
    FT8_send_long(DL_END, 0, 0, 1);

    if (cmd_burst == 0) eve_deselect();
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

	if (cmd_burst == 0) eve_deselect();
}


// ============================================================================================
// ==== init, has to be executed with the SPI setup to 11 MHz or less as required by FT8xx ====
// ============================================================================================

//---------------------------------------------
static void EVE_PinsInit(eve_config_t *dconfig)
{
    // Route all used pins to GPIO control
    if (!eve_spibus_is_init) {
        gpio_pad_select_gpio(dconfig->miso);
        gpio_pad_select_gpio(dconfig->mosi);
        gpio_pad_select_gpio(dconfig->sck);

        gpio_set_direction(dconfig->miso, GPIO_MODE_INPUT);
        gpio_set_pull_mode(dconfig->miso, GPIO_PULLUP_ONLY);
        gpio_set_direction(dconfig->mosi, GPIO_MODE_OUTPUT);
        gpio_set_direction(dconfig->sck, GPIO_MODE_OUTPUT);
    }

    gpio_pad_select_gpio(dconfig->cs);
    gpio_set_direction(dconfig->cs, GPIO_MODE_OUTPUT);
    gpio_set_level(dconfig->cs, 1);

    if (dconfig->pd >= 0) {
        gpio_pad_select_gpio(dconfig->pd);
        gpio_set_direction(dconfig->pd, GPIO_MODE_OUTPUT);
        gpio_set_level(dconfig->pd, 0);
    }
    eve_spibus_is_init = 1;
}

//-------------------------------------------------
static esp_err_t EVE_spiInit(eve_config_t *dconfig)
{
    esp_err_t ret;

    /*int used_spi = spi_host_used_by_sdspi();
    if ((used_spi != 0) && (used_spi == dconfig->host)) {
        // change spi host
        if (used_spi == VSPI_HOST) eve_spi->spihost = HSPI_HOST;
        else eve_spi->spihost = VSPI_HOST;
        ESP_LOGW(TAG, "spi bus changed (%d -> %d)", used_spi, eve_spi->spihost);
    }
    else eve_spi->spihost = dconfig->host;*/

    eve_spi->spihost = dconfig->host;
    ESP_LOGD(TAG, "Using spi bus %d", eve_spi->spihost);
    eve_spi->buscfg = SPIbus_configs[eve_spi->spihost];
    if (eve_spi->buscfg == NULL) {
        ESP_LOGE(TAG, "spi bus %d not available ", eve_spi->spihost);
        return ESP_ERR_INVALID_ARG;
    }

    eve_spi->dma_channel = 1;
    eve_spi->curr_clock = 8000000;                        // for initialization set the clock to 8MHz
    eve_spi->handle = NULL;
    eve_spi->cs = dconfig->cs;
    eve_spi->dc = 0;
    eve_spi->selected = 0;

    eve_spi->buscfg->miso_io_num = dconfig->miso;          // set SPI MISO pin
    eve_spi->buscfg->mosi_io_num = dconfig->mosi;          // set SPI MOSI pin
    eve_spi->buscfg->sclk_io_num = dconfig->sck;           // set SPI CLK pin
    eve_spi->buscfg->quadwp_io_num = -1;
    eve_spi->buscfg->quadhd_io_num = -1;
    eve_spi->buscfg->max_transfer_sz = 6*1024;

    eve_spi->devcfg.clock_speed_hz = eve_spi->curr_clock;  // Initial clock
    eve_spi->devcfg.duty_cycle_pos = 128;                  // 50% duty cycle
    eve_spi->devcfg.mode = 0;                              // SPI mode 0
    eve_spi->devcfg.spics_io_num = -1;                     // we will use external CS pin
    eve_spi->devcfg.queue_size = 1;                        // we need only one transaction
    eve_spi->devcfg.flags = 0;                             // ALWAYS SET to FULL DUPLEX MODE for EVE display spi

    // ==================================================================
    // ==== Initialize the SPI bus and attach the LCD to the SPI bus ====
    ret = add_extspi_device(eve_spi);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGD(TAG, "SPI bus configured (%d)", eve_spi->spihost);
    spi_is_init = 1;

    // ==== Test select/deselect ====
    ret = spi_device_select(eve_spi, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error selecting display device");
    }
    vTaskDelay(10 / portTICK_RATE_MS);
    ret = spi_device_deselect(eve_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error deselecting display device");
    }

    ESP_LOGI(TAG, "Attached display device, speed=%u", spi_get_speed(eve_spi));
    ESP_LOGI(TAG, "Bus uses native pins: %s", spi_uses_native_pins(eve_spi->handle) ? "true" : "false");

    return ESP_OK;
}

//----------------------------------------
static bool _ft8xx_detect(uint32_t reg_id)
{
    uint8_t chipid = 0;
    uint8_t timeout = 0;
    // if chip id is not 0x7c, continue to read it until it is,
    // FT81x may need a moment for it's power on self test
    while (chipid != 0x7C) {
        chipid = FT8_memRead8(reg_id);
        vTaskDelay(1 / portTICK_RATE_MS);
        timeout++;
        if (timeout > 200) return false;
    }
    return true;
}
//============================================================================
esp_err_t FT8_init(eve_config_t *dconfig, exspi_device_handle_t *disp_spi_dev)
{
	uint32_t chip_type = 0;
    esp_err_t ret;

    eve_spi = disp_spi_dev;
    if (spi_is_init == 0) {
        EVE_PinsInit(dconfig);
        ret = EVE_spiInit(dconfig);
        if (ret != ESP_OK) {
            eve_spi->handle = NULL;
            eve_spi = NULL;
            return ret;
        }
    }

    vTaskDelay(6 / portTICK_RATE_MS);
	if (dconfig->pd >= 0) {
        gpio_set_level(dconfig->pd, 0);
        vTaskDelay(7 / portTICK_RATE_MS);   // minimum time for power-down is 5ms
        gpio_set_level(dconfig->pd, 1);
        vTaskDelay(25 / portTICK_RATE_MS);  // minimum time to allow from rising PD_N to first access is 20ms
	}
	else {
        FT8_cmdWrite(FT8_CORERST);          // reset, only required for warm start if PowerDown line is not used
        vTaskDelay(25 / portTICK_RATE_MS);  // minimum time to allow from rising PD_N to first access is 20ms
	}

    #if defined (FT8_ADAM101)
    FT8_memWrite8(REG_PWM_DUTY, 0x80);  // turn off backlight for Glyn ADAM101 module, it uses inverted values
    #else
    FT8_memWrite8(REG_PWM_DUTY, 0);     // turn off backlight for any other module
    #endif

    // Setup FT8xx for external or internal clock
	if (dconfig->disp_config.has_crystal != 0) FT8_cmdWrite(FT8_CLKEXT);
	else FT8_cmdWrite(FT8_CLKINT);

	// Start FT8xx
	FT8_cmdWrite(FT8_ACTIVE);

	// Detect EVE chip
	if (!_ft8xx_detect(REG_ID)) {
        #ifdef CONFIG_EVE_CHIP_TYPE1
	    if (_ft8xx_detect(REG_ID_FT80X)) {
            ESP_LOGE(TAG, "EVE module configured for FT81x but FT80x detected");
	    }
        else {
            ESP_LOGE(TAG, "EVE chip not detected (timeout)");
        }
        #else
        if (_ft8xx_detect(REG_ID_FT81X)) {
            ESP_LOGE(TAG, "EVE module configured for FT80x but FT81x detected");
        }
        else {
            ESP_LOGE(TAG, "EVE chip not detected (timeout)");
        }
        #endif
        eve_chip_id = 0;
        return ESP_FAIL;
	}

	// Get chip model
	chip_type = FT8_memRead32(FT8_ROM_CHIPID);
	eve_chip_id = ((chip_type & 0xFF) << 8) | ((chip_type & 0xFF00) >> 8);
	if (((eve_chip_id >= 0x800) && (eve_chip_id <= 0x801)) || ((eve_chip_id >= 0x810) && (eve_chip_id <= 0x813))) {
	    ESP_LOGD(TAG, "EVE chip detected: FT%4x", eve_chip_id);
	}
	else {
        ESP_LOGE(TAG, "EVE chip not detected (wrong ID)");
        eve_chip_id = 0;
        return ESP_FAIL;
	}

    #ifdef CONFIG_EVE_CHIP_TYPE1
    // If we have a display with a Goodix GT911 / GT9271 touch-controller on it, we need to patch our FT811 or FT813 according to AN_336
    if (dconfig->disp_config.has_GT911) {
        uint32_t ftAddress;

        FT8_get_cmdoffset();
        ftAddress = FT8_RAM_CMD + eve_cmdOffset;

        FT8_memWrite_flash_buffer(ftAddress, FT8_GT911_data, FT8_GT911_len, true);

        FT8_cmd_execute(250);

        FT8_memWrite8(REG_TOUCH_OVERSAMPLE, 0x0f);              // setup oversample to 0x0f as "hidden" in binary-blob for AN_336
        FT8_memWrite16(REG_TOUCH_CONFIG, 0x05D0);               // write magic cookie as requested by AN_336

        // specific to the EVE2 modules from Matrix-Orbital we have to use GPIO3 to reset GT911
        FT8_memWrite16(REG_GPIOX_DIR,0x8008);                   // Reset-Value is 0x8000, adding 0x08 sets GPIO3 to output, default-value for REG_GPIOX is 0x8000 -> Low output on GPIO3
        vTaskDelay(2 / portTICK_RATE_MS);                       // wait more than 100µs
        FT8_memWrite8(REG_CPURESET, 0x00);                      // clear all resets
        vTaskDelay(57 / portTICK_RATE_MS);                      // wait more than 55ms
        FT8_memWrite16(REG_GPIOX_DIR,0x8000);                   // setting GPIO3 back to input
    }
    #endif

    //FT8_memWrite8(REG_PCLK, 0x00);	    			        // set PCLK to zero - don't clock the LCD until later

	// === Initialize the display timings ===
	FT8_memWrite16(REG_HSIZE,   dconfig->disp_config.hsize);	// active display width
	FT8_memWrite16(REG_HCYCLE,  dconfig->disp_config.hcycle);	// total number of clocks per line, including front/back porch
	FT8_memWrite16(REG_HOFFSET, dconfig->disp_config.hoffset);	// start of active line
	FT8_memWrite16(REG_HSYNC0,  dconfig->disp_config.hsync0);	// start of horizontal sync pulse
	FT8_memWrite16(REG_HSYNC1,  dconfig->disp_config.hsync1);	// end of horizontal sync pulse
	FT8_memWrite16(REG_VSIZE,   dconfig->disp_config.vsize);		// active display height
	FT8_memWrite16(REG_VCYCLE,  dconfig->disp_config.vcycle);	// total number of lines per screen, incl pre/post
	FT8_memWrite16(REG_VOFFSET, dconfig->disp_config.voffset);	// start of active screen
	FT8_memWrite16(REG_VSYNC0,  dconfig->disp_config.vsync0);	// start of vertical sync pulse
	FT8_memWrite16(REG_VSYNC1,  dconfig->disp_config.vsync1);	// end of vertical sync pulse
	FT8_memWrite8(REG_SWIZZLE,  dconfig->disp_config.swizzle);	// FT8xx output to LCD - pin order
	FT8_memWrite8(REG_PCLK_POL, dconfig->disp_config.pclkpol);	// LCD data is clocked in on this PCLK edge
    FT8_memWrite8(REG_CSPREAD,  dconfig->disp_config.cspread);   // helps with noise, when set to 1 fewer signals are changed simultaneously, reset-default: 1

    // Don't set PCLK yet - wait for just after the first display list

	// ** Configure Touch
	FT8_memWrite8(REG_TOUCH_MODE, FT8_TMODE_CONTINUOUS);	                // enable touch
	FT8_memWrite16(REG_TOUCH_RZTHRESH, dconfig->disp_config.touch_thresh);	// eliminate any false touches

    // disable Audio for now
    FT8_memWrite8(REG_VOL_PB, 0x00);            // turn recorded audio volume down
    FT8_memWrite8(REG_VOL_SOUND, 0x00);         // turn synthesizer volume off
    FT8_memWrite16(REG_SOUND, 0x6000);          //  set synthesizer to mute

    // write a basic display-list to get things started
    FT8_memWrite32(FT8_RAM_DL, DL_CLEAR_RGB);
    FT8_memWrite32(FT8_RAM_DL + 4, (DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG));
    FT8_memWrite32(FT8_RAM_DL + 8, DL_DISPLAY); // end of display list
    FT8_memWrite32(REG_DLSWAP, FT8_DLSWAP_FRAME);

	// nothing is being displayed yet... the pixel clock is still 0x00
	FT8_memWrite8(REG_GPIO, 0x80);		                // enable the DISP signal to the LCD panel
	FT8_memWrite8(REG_PCLK, dconfig->disp_config.pclk);	// now start clocking data to the LCD panel

    do {
        vTaskDelay(2 / portTICK_RATE_MS);       // just to be safe
    } while (FT8_busy());

    FT8_memWrite8(REG_PWM_DUTY, 64);            // turn on backlight
    FT8_get_cmdoffset();

    return ESP_OK;
}

#endif // CONFIG_MICROPY_USE_EVE
