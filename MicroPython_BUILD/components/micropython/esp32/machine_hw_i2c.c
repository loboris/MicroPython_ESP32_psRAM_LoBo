/*
 * This file is derived from the MicroPython project, http://micropython.org/
 *
 * Copyright (c) 2016, Pycom Limited and its licensors.
 *
 * This software is licensed under the GNU GPL version 3 or any later version,
 * with permitted additional terms. For more information see the Pycom Licence
 * v1.0 document supplied with this file, or available at:
 * https://www.pycom.io/opensource/licensing
 */

/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George on behalf of Pycom Ltd
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

#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#include "py/mpstate.h"
#include "py/runtime.h"
#include "py/obj.h"

#include "machine_pin.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"

#define I2C_MASTER_TX_BUF_DISABLE   0   // I2C master does not need buffer
#define I2C_MASTER_RX_BUF_DISABLE   0   // I2C master does not need buffer
#define I2C_ACK_CHECK_EN            (1)
#define I2C_ACK_VAL                 (0)
#define I2C_NACK_VAL                (1)

typedef struct _machine_hw_i2c_obj_t {
    mp_obj_base_t base;
    uint32_t speed;
    uint8_t scl;
    uint8_t sda;
    int8_t bus_id;
    i2c_cmd_handle_t cmd;
} machine_hw_i2c_obj_t;


STATIC const uint8_t mach_i2c_def_pin[2] = {12, 13};


const mp_obj_type_t machine_hw_i2c_type;

//------------------------------------------------------------------
STATIC void hw_i2c_initialise_master (machine_hw_i2c_obj_t *i2c_obj)
{
    i2c_config_t conf;

    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = i2c_obj->sda;
    conf.scl_io_num = i2c_obj->scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = i2c_obj->speed;

    i2c_param_config(i2c_obj->bus_id, &conf);
    i2c_driver_install(i2c_obj->bus_id, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------
STATIC void hw_i2c_master_writeto(machine_hw_i2c_obj_t *i2c_obj, uint16_t slave_addr, bool memwrite, uint32_t memaddr, uint8_t *data, uint16_t len, bool stop)
{
	esp_err_t ret = ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK) goto error;
    if (i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN) != ESP_OK) goto error;

    if (memwrite) {
        if (memaddr > 0xFF) {
        	if (i2c_master_write_byte(cmd, (memaddr >> 8), I2C_ACK_CHECK_EN) != ESP_OK) goto error;
        }
        if (i2c_master_write_byte(cmd, memaddr, I2C_ACK_CHECK_EN) != ESP_OK) goto error;
    }

    if (i2c_master_write(cmd, data, len, I2C_ACK_CHECK_EN) != ESP_OK) goto error;
    if (stop) {
        if (i2c_master_stop(cmd) != ESP_OK) goto error;
    }

    ret = i2c_master_cmd_begin(i2c_obj->bus_id, cmd, (5000 + (1000 * len)) / portTICK_RATE_MS);

error:
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus error"));
    }
}

//-------------------------------------------------------------------------------------------------------------------------------------------------
STATIC void hw_i2c_master_readfrom(machine_hw_i2c_obj_t *i2c_obj, uint16_t slave_addr, bool memread, uint32_t memaddr, uint8_t *data, uint16_t len)
{
	esp_err_t ret = ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK) {ret=1; goto error;};

    if (memread) {
        if (i2c_master_write_byte(cmd, ( slave_addr << 1 ) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN) != ESP_OK) {ret=2; goto error;};
        if (memaddr > 0xFF) {
            if (i2c_master_write_byte(cmd, (memaddr >> 8), I2C_ACK_CHECK_EN) != ESP_OK) {ret=3; goto error;};
        }
        if (i2c_master_write_byte(cmd, memaddr, I2C_ACK_CHECK_EN) != ESP_OK) {ret=4; goto error;};

        // repeated start
        if (i2c_master_start(cmd) != ESP_OK) {ret=5; goto error;};
    }

    if (i2c_master_write_byte(cmd, ( slave_addr << 1 ) | I2C_MASTER_READ, I2C_ACK_CHECK_EN) != ESP_OK) {ret=6; goto error;};

    if (len > 1) {
        if (i2c_master_read(cmd, data, len - 1, I2C_ACK_VAL) != ESP_OK) {ret=7; goto error;};
    }

    if (i2c_master_read_byte(cmd, data + len - 1, I2C_NACK_VAL) != ESP_OK) {ret=8; goto error;};
    if (i2c_master_stop(cmd) != ESP_OK) {ret=8; goto error;};

    ret = i2c_master_cmd_begin(i2c_obj->bus_id, cmd, (5000 + (1000 * len)) / portTICK_RATE_MS);

error:
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
    	char errs[32];
    	sprintf(errs, "I2C bus error (%d)", ret);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, errs));
    }
}

//--------------------------------------------------------------------------------
STATIC bool hw_i2c_slave_ping (machine_hw_i2c_obj_t *i2c_obj, uint16_t slave_addr)
{
	esp_err_t ret = ESP_FAIL;

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK) goto error;
    if (i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN) != ESP_OK) goto error;
    if (i2c_master_stop(cmd) != ESP_OK) goto error;
    ret = i2c_master_cmd_begin(i2c_obj->bus_id, cmd, 500 / portTICK_RATE_MS);

error:
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK) ? true : false;
}


/******************************************************************************/
// MicroPython bindings for I2C

//-----------------------------------------------------------------------------------------------
STATIC void machine_hw_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    machine_hw_i2c_obj_t *self = self_in;
    mp_printf(print, "I2C(Port=%u, Mode=MASTER, speed=%uHz, sda=GPIO_NUM_%d, scl=GPIO_NUM_%d)", self->bus_id, self->speed, self->sda, self->scl);
}

//---------------------------------------------------------
STATIC const mp_arg_t machine_hw_i2c_init_allowed_args[] = {
		{ MP_QSTR_id,                      MP_ARG_INT, {.u_int = 0} },
		{ MP_QSTR_mode,                    MP_ARG_INT, {.u_int = I2C_MODE_MASTER} },
		{ MP_QSTR_speed, MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 100000} },
		{ MP_QSTR_freq,  MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_sda,   MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
		{ MP_QSTR_scl,   MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
};

//---------------------------------------------------------------------------------------------------------------
mp_obj_t machine_hw_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	enum { ARG_id, ARG_mode, ARG_speed, ARG_freq, ARG_sda, ARG_scl };

	mp_arg_val_t args[MP_ARRAY_SIZE(machine_hw_i2c_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(machine_hw_i2c_init_allowed_args), machine_hw_i2c_init_allowed_args, args);

    int8_t sda;
    int8_t scl;
    int8_t bus_id = args[ARG_id].u_int;
    uint32_t speed;
    if (args[ARG_freq].u_int > 0) speed = args[ARG_freq].u_int;
    else speed = args[ARG_speed].u_int;

    // Check the peripheral id
    if (bus_id < 0 || bus_id > 1) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus not available"));
    }
    // Check mode
    if (args[ARG_mode].u_int != I2C_MODE_MASTER) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Only I2C MASTER mode available for now"));
    }

    if (args[ARG_sda].u_obj == MP_OBJ_NULL) {
        sda = mach_i2c_def_pin[0];
    } else {
        sda = machine_pin_get_id(args[ARG_sda].u_obj);
    }
    if (args[ARG_scl].u_obj == MP_OBJ_NULL) {
        scl = mach_i2c_def_pin[1];
    } else {
        scl = machine_pin_get_id(args[ARG_scl].u_obj);
    }

    // Setup the I2C object
    machine_hw_i2c_obj_t *self = m_new_obj(machine_hw_i2c_obj_t );
    self->base.type = &machine_hw_i2c_type;
    self->bus_id = bus_id;
    self->speed = speed;
    self->scl = scl;
    self->sda = sda;

    hw_i2c_initialise_master(self);

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_init(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    machine_hw_i2c_obj_t *self = pos_args[0];
	enum { ARG_id, ARG_mode, ARG_speed, ARG_freq, ARG_sda, ARG_scl };

	mp_arg_val_t args[MP_ARRAY_SIZE(machine_hw_i2c_init_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(machine_hw_i2c_init_allowed_args), machine_hw_i2c_init_allowed_args, args);

    int8_t sda;
    int8_t scl;
    int8_t bus_id = args[ARG_id].u_int;
    uint32_t speed;
    if (args[ARG_freq].u_int > 0) speed = args[ARG_freq].u_int;
    else speed = args[ARG_speed].u_int;
    uint8_t changed = 0;

    // Check the peripheral id
    if (bus_id < 0 || bus_id > 1) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus not available"));
    }
    // Check mode
    if (args[ARG_mode].u_int != I2C_MODE_MASTER) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Only I2C MASTER mode available for now"));
    }

    if (args[ARG_sda].u_obj == MP_OBJ_NULL) {
        sda = mach_i2c_def_pin[0];
    } else {
        sda = machine_pin_get_id(args[ARG_sda].u_obj);
    }
    if (args[ARG_scl].u_obj == MP_OBJ_NULL) {
        scl = mach_i2c_def_pin[1];
    } else {
        scl = machine_pin_get_id(args[ARG_scl].u_obj);
    }

    if (self->bus_id != bus_id) changed++;
    if (self->speed != speed) changed++;
    if (self->scl != scl) changed++;
    if (self->sda != sda) changed++;

    if (changed) {
    	if (self->speed > 0) i2c_driver_delete(self->bus_id);

    	self->bus_id = bus_id;
		self->speed = speed;
		self->scl = scl;
		self->sda = sda;
		hw_i2c_initialise_master(self);
    }

    return MP_OBJ_FROM_PTR(self);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_i2c_init_obj, 1, machine_hw_i2c_init);

//---------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_scan(mp_obj_t self_in)
{
    machine_hw_i2c_obj_t *self = self_in;
    mp_obj_t list = mp_obj_new_list(0, NULL);

    // 7-bit addresses 0b0000xxx and 0b1111xxx are reserved

	for (int addr = 0x08; addr <= 0x78; ++addr) {
		if (hw_i2c_slave_ping(self, addr)) {
			mp_obj_list_append(list, MP_OBJ_NEW_SMALL_INT(addr));
		}
	}
    return list;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_hw_i2c_scan_obj, machine_hw_i2c_scan);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_readfrom(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    STATIC const mp_arg_t machine_i2c_readfrom_args[] = {
        { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_nbytes,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
    };

    machine_hw_i2c_obj_t *self = pos_args[0];

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_readfrom_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(args), machine_i2c_readfrom_args, args);

    vstr_t vstr;
    vstr_init_len(&vstr, args[1].u_int);
    hw_i2c_master_readfrom(self, args[0].u_int, false, 0, (uint8_t*)vstr.buf, vstr.len);

    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_i2c_readfrom_obj, 1, machine_hw_i2c_readfrom);

//-----------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_readfrom_into(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_hw_i2c_obj_t *self = pos_args[0];

    STATIC const mp_arg_t machine_i2c_readfrom_into_args[] = {
        { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
        { MP_QSTR_buf,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_readfrom_into_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(args), machine_i2c_readfrom_into_args, args);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1].u_obj, &bufinfo, MP_BUFFER_WRITE);

    hw_i2c_master_readfrom(self, args[0].u_int, false, 0, bufinfo.buf, bufinfo.len);

    return mp_obj_new_int(bufinfo.len);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_i2c_readfrom_into_obj, 1, machine_hw_i2c_readfrom_into);

//-----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_writeto(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_hw_i2c_obj_t *self = pos_args[0];

    STATIC const mp_arg_t machine_i2c_writeto_args[] = {
        { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_buf,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_stop,    MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = true} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_writeto_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(args), machine_i2c_writeto_args, args);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1].u_obj, &bufinfo, MP_BUFFER_READ);

    hw_i2c_master_writeto(self, args[0].u_int, false, 0x0, bufinfo.buf, bufinfo.len, args[2].u_bool);

    return mp_obj_new_int(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_i2c_writeto_obj, 1, machine_hw_i2c_writeto);

//---------------------------------------------------------
STATIC const mp_arg_t machine_hw_i2c_mem_allowed_args[] = {
    { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_memaddr, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_arg,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
};

//-------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_readfrom_mem(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_memaddr, ARG_n };
    machine_hw_i2c_obj_t *self = pos_args[0];
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_hw_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(machine_hw_i2c_mem_allowed_args), machine_hw_i2c_mem_allowed_args, args);

    int n = mp_obj_get_int(args[ARG_n].u_obj);
    if (n > 0) {
        // create the buffer to store data into
        vstr_t vstr;
        vstr_init_len(&vstr, n);

        // do the transfer
        //uint8_t addr = args[ARG_memaddr].u_int;
        //hw_i2c_master_writeto(self, args[0].u_int, false, 0x0, &addr, 1, true);
        //hw_i2c_master_readfrom(self, args[0].u_int, false, 0, (uint8_t*)vstr.buf, vstr.len);
        hw_i2c_master_readfrom(self, args[ARG_addr].u_int, true, args[ARG_memaddr].u_int, (uint8_t *)vstr.buf, vstr.len);
        return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
    }
    return mp_const_empty_bytes;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_i2c_readfrom_mem_obj, 1, machine_hw_i2c_readfrom_mem);

//------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_readfrom_mem_into(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_memaddr, ARG_buf };
    machine_hw_i2c_obj_t *self = pos_args[0];
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_hw_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(machine_hw_i2c_mem_allowed_args), machine_hw_i2c_mem_allowed_args, args);

    // get the buffer to store data into
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_buf].u_obj, &bufinfo, MP_BUFFER_WRITE);

    if (bufinfo.len > 0) {
        // do the transfer
        hw_i2c_master_readfrom(self, args[ARG_addr].u_int, true, args[ARG_memaddr].u_int, bufinfo.buf, bufinfo.len);
    }
    return mp_obj_new_int(bufinfo.len);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_i2c_readfrom_mem_into_obj, 1, machine_hw_i2c_readfrom_mem_into);

//------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_writeto_mem(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_memaddr, ARG_buf };
    machine_hw_i2c_obj_t *self = pos_args[0];
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_hw_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(machine_hw_i2c_mem_allowed_args), machine_hw_i2c_mem_allowed_args, args);

    // get the buffer to write the data from
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_buf].u_obj, &bufinfo, MP_BUFFER_READ);

    // do the transfer
    hw_i2c_master_writeto(self, args[ARG_addr].u_int, true, args[ARG_memaddr].u_int, bufinfo.buf, bufinfo.len, true);

    return mp_obj_new_int(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_hw_i2c_writeto_mem_obj, 1, machine_hw_i2c_writeto_mem);

//-------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_deinit(mp_obj_t self_in) {
    machine_hw_i2c_obj_t *self = self_in;

    if (self->speed > 0) {
    	i2c_driver_delete(self->bus_id);
        // invalidate the speed
        self->speed = 0;
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_hw_i2c_deinit_obj, machine_hw_i2c_deinit);

/*
//------------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_start(mp_obj_t self_in) {
    machine_hw_i2c_obj_t *self = self_in;

    if (self->cmd == NULL) {
        self->cmd = i2c_cmd_link_create();
    }
    if (i2c_master_start(self->cmd) != ESP_OK) {
    	i2c_cmd_link_delete(self->cmd);
    	self->cmd = NULL;
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus error"));
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_hw_i2c_start_obj, machine_hw_i2c_start);

//-----------------------------------------------------
STATIC mp_obj_t machine_hw_i2c_stop(mp_obj_t self_in) {
    machine_hw_i2c_obj_t *self = self_in;

    if (self->cmd == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C: stop before start!"));
    }
    if (i2c_master_stop(self->cmd) != ESP_OK) {
    	i2c_cmd_link_delete(self->cmd);
    	self->cmd = NULL;
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus error"));
    }
	i2c_cmd_link_delete(self->cmd);
	self->cmd = NULL;

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_hw_i2c_stop_obj, machine_hw_i2c_stop);
*/

//===================================================================
STATIC const mp_rom_map_elem_t machine_hw_i2c_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),                (mp_obj_t)&machine_hw_i2c_init_obj },
    { MP_ROM_QSTR(MP_QSTR_deinit),              (mp_obj_t)&machine_hw_i2c_deinit_obj },
    { MP_ROM_QSTR(MP_QSTR_scan),                (mp_obj_t)&machine_hw_i2c_scan_obj },

    // standard bus operations
    { MP_ROM_QSTR(MP_QSTR_readfrom),            (mp_obj_t)&machine_hw_i2c_readfrom_obj },
    { MP_ROM_QSTR(MP_QSTR_readfrom_into),       (mp_obj_t)&machine_hw_i2c_readfrom_into_obj },
    { MP_ROM_QSTR(MP_QSTR_writeto),             (mp_obj_t)&machine_hw_i2c_writeto_obj },

    // memory operations
    { MP_ROM_QSTR(MP_QSTR_readfrom_mem),        (mp_obj_t)&machine_hw_i2c_readfrom_mem_obj },
    { MP_ROM_QSTR(MP_QSTR_readfrom_mem_into),   (mp_obj_t)&machine_hw_i2c_readfrom_mem_into_obj },
    { MP_ROM_QSTR(MP_QSTR_writeto_mem),         (mp_obj_t)&machine_hw_i2c_writeto_mem_obj },

    { MP_OBJ_NEW_QSTR(MP_QSTR_MASTER),          MP_OBJ_NEW_SMALL_INT(I2C_MODE_MASTER) },
};

STATIC MP_DEFINE_CONST_DICT(machine_hw_i2c_locals_dict, machine_hw_i2c_locals_dict_table);

//=========================================
const mp_obj_type_t machine_hw_i2c_type = {
    { &mp_type_type },
    .name = MP_QSTR_I2C,
    .print = machine_hw_i2c_print,
    .make_new = machine_hw_i2c_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_hw_i2c_locals_dict,
};
