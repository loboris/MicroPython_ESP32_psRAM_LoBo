/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
 * Copyright 2017-2018 LoBo (https://github.com/loboris)
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "esp_heap_caps.h"

//#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"
#include "modmachine.h"
#include "py/objstr.h"

#include "machine_hw_spi.h"

/*
 * There are two SPI hosts on ESP32 available to the user, HSPI_HOST & VSPI_HOST
 * If psRAM is used and is configured to run at 80 MHz, only HSPI_HOST is available !
 * Each SPI host is configured with miso, mosi & sck pins on which it operates
 * Up to 6 spi devices can be attached to each SPI host, all must use different CS pin
 * CS is handled by the driver using spi_device_select() / spi_device_deselect() functions
 */


//--------------------------------------------------------------------
STATIC void machine_hw_spi_deinit_internal(machine_hw_spi_obj_t *self)
{
    esp_err_t res = remove_extspi_device(&self->spi);
    if (res != ESP_OK) {
    	mp_raise_msg(&mp_type_OSError, "error removing spi device");
    }
}

//--------------------------------
void machine_hw_spi_init_internal(
    machine_hw_spi_obj_t    *self,
    int8_t                  host,
    int32_t                 baudrate,
    int8_t                  polarity,
    int8_t                  phase,
    int8_t                  firstbit,
    int8_t                  sck,
    int8_t                  mosi,
    int8_t                  miso,
    int8_t                  cs,
    int8_t                  duplex,
	uint8_t					queue_size,
	uint8_t					new) {


    esp_err_t ret;

    self->state = MACHINE_HW_SPI_STATE_DEINIT;

	if ((baudrate <= 0) || (baudrate > 80000000)) {
        mp_raise_ValueError("SPI baudrate must be >0 & <=80000000");
	}

	// Host initialization
	if ((host != HSPI_HOST) && (host != VSPI_HOST)) {
		mp_raise_ValueError("SPI host must be either HSPI(1) or VSPI(2)");
	}
	if ((SPIbus_configs[VSPI_HOST] == NULL) && (host == VSPI_HOST)) {
		mp_raise_ValueError("SPI host must be HSPI(1), VSPI(2) used by SPIRAM");
	}

    int used_spi = spi_host_used_by_sdspi();
    if ((used_spi != 0) && (used_spi == host)) {
        // change spi host
        if (host == VSPI_HOST) {
    		mp_raise_ValueError("SPI host must be HSPI(1), VSPI(2) used by SDCard driver");
        }
        else {
    		mp_raise_ValueError("SPI host must be VSPI(2), HSPI(1) used by SDCard driver");
        }
    }

    self->spi.spihost = host;
    self->spi.buscfg = SPIbus_configs[self->spi.spihost];

    // Init pins
    if (cs >= 0) {
		gpio_pad_select_gpio(cs);
		gpio_set_direction(cs, GPIO_MODE_OUTPUT);
		gpio_set_level(cs, 1);
    }
    gpio_pad_select_gpio(miso);
    gpio_pad_select_gpio(mosi);
    gpio_pad_select_gpio(sck);
    gpio_set_direction(miso, GPIO_MODE_INPUT);
    gpio_set_pull_mode(miso, GPIO_PULLUP_ONLY);
    gpio_set_direction(mosi, GPIO_MODE_OUTPUT);
    gpio_set_direction(sck, GPIO_MODE_OUTPUT);

    // Set configuration
    self->polarity = polarity & 1;
    self->phase = phase & 1;
    self->firstbit = firstbit & 1;
    self->duplex = duplex & 1;

    self->spi.dma_channel = 1;
    self->spi.curr_clock = baudrate;
    self->spi.handle = NULL;
    self->spi.dc = -1;
    self->spi.selected = 0;
    self->spi.cs = cs;

    self->spi.buscfg->miso_io_num = miso;
    self->spi.buscfg->mosi_io_num = mosi;
    self->spi.buscfg->sclk_io_num = sck;
    self->spi.buscfg->quadwp_io_num = -1;
    self->spi.buscfg->quadhd_io_num = -1;
    //self->spi.buscfg->max_transfer_sz = 6*1024;

    self->spi.devcfg.clock_speed_hz = self->spi.curr_clock;
    self->spi.devcfg.duty_cycle_pos = 128;
    self->spi.devcfg.mode = self->phase | (self->polarity << 1);
    self->spi.devcfg.spics_io_num = -1;
    self->spi.devcfg.queue_size = queue_size;
    self->spi.devcfg.flags = ((self->firstbit == MICROPY_PY_MACHINE_SPI_LSB) ? SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_RXBIT_LSBFIRST : 0);
    if (!self->duplex) self->spi.devcfg.flags |= SPI_DEVICE_HALFDUPLEX;
    self->spi.devcfg.pre_cb = NULL;

	// ==== Initialize the SPI bus and attach the device ====
	ret = add_extspi_device(&self->spi);
	if (ret == ESP_OK) {
		ret = spi_device_select(&self->spi, 1);
		if (ret != ESP_OK) {
	        machine_hw_spi_deinit_internal(self);
            mp_raise_msg(&mp_type_OSError, "Error selecting device");
		}
		ret = spi_device_deselect(&self->spi);
		if (ret != ESP_OK) {
	        machine_hw_spi_deinit_internal(self);
            mp_raise_msg(&mp_type_OSError, "Error deselecting device");
		}
	    self->state = MACHINE_HW_SPI_STATE_INIT;
	    return;
	}

	switch (ret) {
        case ESP_ERR_INVALID_ARG:
            mp_raise_msg(&mp_type_OSError, "invalid configuration");
            return;
        case ESP_ERR_INVALID_STATE:
            mp_raise_msg(&mp_type_OSError, "invalid state");
            return;
        case ESP_ERR_NO_MEM:
            mp_raise_msg(&mp_type_OSError, "out of memory");
            return;
        case ESP_ERR_NOT_FOUND:
            mp_raise_msg(&mp_type_OSError, "no free slots");
            return;
        default:
        {
        	char err[40];
        	sprintf(err, "error initializing spi (%d)", ret);
			mp_raise_msg(&mp_type_OSError, err);
			return;
        }
    }
}

//----------------------------------------------
STATIC void checkSPI(machine_hw_spi_obj_t *self)
{
    if (self->state != MACHINE_HW_SPI_STATE_INIT) {
        mp_raise_msg(&mp_type_OSError, "SPI not initialized");
    }
}

//-----------------------------------------------------
STATIC mp_obj_t machine_hw_spi_deinit(mp_obj_t self_in)
{
    machine_hw_spi_obj_t *self = self_in;
    if (self->state == MACHINE_HW_SPI_STATE_INIT) {
        self->state = MACHINE_HW_SPI_STATE_DEINIT;
        machine_hw_spi_deinit_internal(self);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_hw_spi_deinit_obj, machine_hw_spi_deinit);

//-----------------------------------------------------------------------------------------------
STATIC void machine_hw_spi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    machine_hw_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->state == MACHINE_HW_SPI_STATE_DEINIT) {
        mp_printf(print, "SPI( DEINITIALIZED )");
        return;
    }
    char scs[16] = {'\0'};
    if (self->spi.cs < 0) sprintf(scs, "Not used");
    else sprintf(scs, "%d", self->spi.cs);

    mp_printf(print, "SPI( %s\n     spihost=%s (%s)\n     baudrate=%u, polarity=%u, phase=%u, firstbit=%s, duplex=%s\n     Pins: sck=%d, mosi=%d, miso=%d, cs=%s\n   )",
    		(self->state == MACHINE_HW_SPI_STATE_INIT) ? "INITIALIZED" : "DEINITIALIZED",
    		(self->spi.spihost == HSPI_HOST) ? "HSPI" : "VSPI", (self->spi.buscfg->mosi_io_num < 0) ? "free" : "initialized",
    		spi_get_speed(&self->spi), self->polarity, self->phase, (self->firstbit) ? "LSB" : "MSB", (self->duplex) ? "True" : "False",
			self->spi.buscfg->sclk_io_num, self->spi.buscfg->mosi_io_num, self->spi.buscfg->miso_io_num, scs);
}

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_spi_init(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
   return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_spi_init_obj, 0, machine_hw_spi_init);

//---------------------------------------------------------------------------------------------------------------
mp_obj_t machine_hw_spi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_spihost, ARG_baudrate, ARG_polarity, ARG_phase, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso, ARG_cs, ARG_duplex };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_spihost,  MP_ARG_REQUIRED | MP_ARG_INT , {.u_int = HSPI_HOST} },
        { MP_QSTR_baudrate, MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 1000000} },
        { MP_QSTR_polarity, MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_phase,    MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_firstbit, MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = MICROPY_PY_MACHINE_SPI_MSB} },
        { MP_QSTR_sck,      MP_ARG_KW_ONLY  | MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_mosi,     MP_ARG_KW_ONLY  | MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_miso,     MP_ARG_KW_ONLY  | MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_cs,       MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_duplex,   MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_bits,     MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 8} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    machine_hw_spi_obj_t *self = m_new_obj(machine_hw_spi_obj_t);
    self->base.type = &machine_hw_spi_type;
    self->state = MACHINE_HW_SPI_STATE_NONE;

    memset(&self->spi.devcfg, 0, sizeof(spi_device_interface_config_t));

    self->spi.cs = -1;
    self->spi.handle = NULL;
    int8_t cs = -1;
    if (args[ARG_cs].u_obj != MP_OBJ_NULL) cs = machine_pin_get_gpio(args[ARG_cs].u_obj);

    machine_hw_spi_init_internal(
        self,
        args[ARG_spihost].u_int,
        args[ARG_baudrate].u_int,
        args[ARG_polarity].u_int,
        args[ARG_phase].u_int,
        args[ARG_firstbit].u_int,
        machine_pin_get_gpio(args[ARG_sck].u_obj),
        machine_pin_get_gpio(args[ARG_mosi].u_obj),
        machine_pin_get_gpio(args[ARG_miso].u_obj),
    	cs,
        args[ARG_duplex].u_bool,
		1,
		1);

    return MP_OBJ_FROM_PTR(self);
}

//----------------------------------------------------------------------
STATIC mp_obj_t mp_machine_spi_read(size_t n_args, const mp_obj_t *args)
{
    machine_hw_spi_obj_t *self = args[0];
    checkSPI(self);

    vstr_t vstr;
    vstr_init_len(&vstr, mp_obj_get_int(args[1]));
    memset(vstr.buf, n_args == 3 ? mp_obj_get_int(args[2]) : 0, vstr.len);

	spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    t.rxlength = vstr.len * 8;
    t.rx_buffer = vstr.buf;
    if ((self->duplex) && (n_args == 3)) {
		t.length = vstr.len * 8;
		t.tx_buffer = vstr.buf;
    }
    else {
		t.length = 0;
		t.tx_buffer = NULL;
    }

	esp_err_t ret = spi_transfer_data_nodma(&self->spi, &t);

	if (ret == ESP_OK) {
	    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
	}
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_spi_read_obj, 2, 3, mp_machine_spi_read);

//--------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_spi_readinto(size_t n_args, const mp_obj_t *args)
{
    machine_hw_spi_obj_t *self = args[0];
    checkSPI(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_WRITE);
    memset(bufinfo.buf, n_args == 3 ? mp_obj_get_int(args[2]) : 0, bufinfo.len);

	spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    t.rxlength = bufinfo.len * 8;
    t.rx_buffer = bufinfo.buf;
    if (n_args == 3) {
		t.length = bufinfo.len * 8;
		t.tx_buffer = bufinfo.buf;
    }
    else {
		t.length = 0;
		t.tx_buffer = NULL;
    }

	esp_err_t ret = spi_transfer_data_nodma(&self->spi, &t);

	if (ret == ESP_OK) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_spi_readinto_obj, 2, 3, mp_machine_spi_readinto);

//---------------------------------------------------------------------
STATIC mp_obj_t mp_machine_spi_write(mp_obj_t self_in, mp_obj_t wr_buf)
{
    machine_hw_spi_obj_t *self = self_in;
    checkSPI(self);

    mp_buffer_info_t src;
    mp_get_buffer_raise(wr_buf, &src, MP_BUFFER_READ);

	spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    t.length = src.len * 8;
    t.tx_buffer = src.buf;
    t.rxlength = 0;
    t.rx_buffer = NULL;

	esp_err_t ret = spi_transfer_data_nodma(&self->spi, &t);

	if (ret == ESP_OK) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_spi_write_obj, mp_machine_spi_write);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_spi_write_readinto(mp_obj_t self_in, mp_obj_t wr_buf, mp_obj_t rd_buf)
{
    machine_hw_spi_obj_t *self = self_in;
    checkSPI(self);

    mp_buffer_info_t src;
    mp_get_buffer_raise(wr_buf, &src, MP_BUFFER_READ);
    mp_buffer_info_t dest;
    mp_get_buffer_raise(rd_buf, &dest, MP_BUFFER_WRITE);

	spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    t.length = src.len * 8;
    t.tx_buffer = src.buf;
    t.rxlength = dest.len * 8;
    t.rx_buffer = dest.buf;
	esp_err_t ret = spi_transfer_data_nodma(&self->spi, &t);

	if (ret == ESP_OK) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_spi_write_readinto_obj, mp_machine_spi_write_readinto);

//---------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_spi_read_from_mem(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    machine_hw_spi_obj_t *self = pos_args[0];
    checkSPI(self);

    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_address,  MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_length,   MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_addrlen,                    MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint8_t wrbuf[4];
    uint8_t *rdbuf = NULL;
    if (args[1].u_int > 0) {
    	rdbuf = malloc(args[1].u_int);
    }
    uint32_t addr = (uint32_t)args[0].u_int;

    int adrlen = args[2].u_int;
    if (adrlen < 1) adrlen = 1;
    if (adrlen > 4) adrlen = 4;

	spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction

    wrbuf[0] = addr & 0xFF;
    wrbuf[1] = (addr >> 8) & 0xFF;
    wrbuf[2] = (addr >> 16) & 0xFF;
    wrbuf[3] = (addr >> 24) & 0xFF;
    t.tx_buffer = &wrbuf[0];
    t.length = adrlen * 8;
    //t.flags = SPI_TRANS_USE_TXDATA;

    t.rxlength = args[1].u_int * 8;
    t.rx_buffer = rdbuf;

	esp_err_t ret = spi_transfer_data_nodma(&self->spi, &t);

	mp_obj_t res = mp_const_none;
	if (ret == ESP_OK) {
		if (rdbuf) res = mp_obj_new_str_of_type(&mp_type_bytes, rdbuf, args[1].u_int);
		else res = mp_obj_new_str_of_type(&mp_type_bytes, (byte *)"", 0);
	}
	else mp_obj_new_str_of_type(&mp_type_bytes, (byte *)"_Error_", 7);

	if (rdbuf) free(rdbuf);

	return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_spi_read_from_mem_obj, 0, mp_machine_spi_read_from_mem);

//-----------------------------------------------------
STATIC mp_obj_t mp_machine_spi_select(mp_obj_t self_in)
{
    machine_hw_spi_obj_t *self = self_in;
    checkSPI(self);

	esp_err_t ret = spi_device_select(&self->spi, 0);

	if (ret == ESP_OK) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_spi_select_obj, mp_machine_spi_select);

//-------------------------------------------------------
STATIC mp_obj_t mp_machine_spi_deselect(mp_obj_t self_in)
{
    machine_hw_spi_obj_t *self = self_in;
    checkSPI(self);

	esp_err_t ret = spi_device_deselect(&self->spi);

	if (ret == ESP_OK) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_spi_deselect_obj, mp_machine_spi_deselect);


//================================================================
STATIC const mp_rom_map_elem_t machine_spi_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),			(mp_obj_t)&machine_hw_spi_init_obj },
    { MP_ROM_QSTR(MP_QSTR_deinit),			(mp_obj_t)&machine_hw_spi_deinit_obj },
    { MP_ROM_QSTR(MP_QSTR_read),			(mp_obj_t)&mp_machine_spi_read_obj },
    { MP_ROM_QSTR(MP_QSTR_readinto),		(mp_obj_t)&mp_machine_spi_readinto_obj },
    { MP_ROM_QSTR(MP_QSTR_readfrom_mem),	(mp_obj_t)&mp_machine_spi_read_from_mem_obj },
    { MP_ROM_QSTR(MP_QSTR_write),			(mp_obj_t)&mp_machine_spi_write_obj },
    { MP_ROM_QSTR(MP_QSTR_write_readinto),	(mp_obj_t)&mp_machine_spi_write_readinto_obj },
    { MP_ROM_QSTR(MP_QSTR_select),			(mp_obj_t)&mp_machine_spi_select_obj },
    { MP_ROM_QSTR(MP_QSTR_deselect),		(mp_obj_t)&mp_machine_spi_deselect_obj },

    { MP_ROM_QSTR(MP_QSTR_MSB),				MP_ROM_INT(MICROPY_PY_MACHINE_SPI_MSB) },
    { MP_ROM_QSTR(MP_QSTR_LSB),				MP_ROM_INT(MICROPY_PY_MACHINE_SPI_LSB) },
	{ MP_ROM_QSTR(MP_QSTR_HSPI),			MP_ROM_INT(HSPI_HOST) },
	{ MP_ROM_QSTR(MP_QSTR_VSPI),			MP_ROM_INT(VSPI_HOST) },
};
MP_DEFINE_CONST_DICT(mp_machine_spi_locals_dict, machine_spi_locals_dict_table);

//=========================================
const mp_obj_type_t machine_hw_spi_type = {
    { &mp_type_type },
    .name = MP_QSTR_SPI,
    .print = machine_hw_spi_print,
    .make_new = machine_hw_spi_make_new,
    .locals_dict = (mp_obj_dict_t *) &mp_machine_spi_locals_dict,
};
