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

#define I2C_ACK_CHECK_EN            (1)
#define I2C_RX_MAX_BUFF_LEN			2048	// maximum low level commands receive buffer length
#define I2C_SLAVE_MAX_BUFF_LEN		2048	// maximum slave buffer length (max 4096 !)
#define I2C_SLAVE_DEFAULT_BUFF_LEN	256		// default slave buffer length
#define I2C_SLAVE_ADDR_DEFAULT		32		// default slave address
#define I2C_SLAVE_MUTEX_TIMEOUT		(500 / portTICK_PERIOD_MS)

typedef struct _mp_machine_i2c_obj_t {
    mp_obj_base_t base;
    uint32_t speed;
    uint8_t mode;
    uint8_t scl;
    uint8_t sda;
    int8_t bus_id;
    i2c_cmd_handle_t cmd;
    uint16_t rx_buflen;			// low level commands receive buffer length
    uint16_t rx_bufidx;			// low level commands receive buffer index
    uint8_t *rx_data;			// low level commands receive buffer
    int8_t slave_addr;			// slave only, slave 8-bit address
    uint16_t slave_buflen;		// slave only, data buffer length
    uint8_t *slave_data;		// slave only, data buffer
    uint32_t *slave_cb_read;	// slave only, slave callback function for read data
    uint32_t *slave_cb_write;	// slave only, slave callback function for write data
    uint32_t *slave_cb_data;	// slave only, slave callback function for data
} mp_machine_i2c_obj_t;


extern int MainTaskCore;

const mp_obj_type_t machine_hw_i2c_type;

static int i2c_used[I2C_MODE_MAX] = { -1, -1 };
static QueueHandle_t slave_mutex = NULL;
static TaskHandle_t i2c_slave_task_handle[I2C_MODE_MAX] = { NULL, NULL };


// ============================================================================================
// === Low level I2C functions using esp-idf i2c-master driver ================================
// ============================================================================================

//---------------------------------------------------------
STATIC esp_err_t i2c_init_master (mp_machine_i2c_obj_t *i2c_obj)
{
    i2c_config_t conf;

    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = i2c_obj->sda;
    conf.scl_io_num = i2c_obj->scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = i2c_obj->speed;

    i2c_param_config(i2c_obj->bus_id, &conf);
    return i2c_driver_install(i2c_obj->bus_id, I2C_MODE_MASTER, 0, 0, 0);
}

//--------------------------------------------------------
STATIC esp_err_t i2c_init_slave (mp_machine_i2c_obj_t *i2c_obj)
{
    i2c_config_t conf;

    conf.mode = I2C_MODE_SLAVE;
    conf.sda_io_num = i2c_obj->sda;
    conf.scl_io_num = i2c_obj->scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.slave.addr_10bit_en = 0;
    conf.slave.slave_addr = i2c_obj->slave_addr;

    i2c_param_config(i2c_obj->bus_id, &conf);
    return i2c_driver_install(i2c_obj->bus_id, conf.mode, i2c_obj->slave_buflen + 8, i2c_obj->slave_buflen + 8, 0);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
STATIC void mp_i2c_master_write(mp_machine_i2c_obj_t *i2c_obj, uint16_t slave_addr, uint8_t memwrite, uint32_t memaddr, uint8_t *data, uint16_t len, bool stop)
{
	esp_err_t ret = ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK) {ret=1; goto error;};
	// send slave address
    if (i2c_master_write_byte(cmd, (slave_addr << 1) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN) != ESP_OK) {ret=2; goto error;};

    while (memwrite > 0) {
    	// send memory address, MSByte first
    	memwrite--;
    	if (i2c_master_write_byte(cmd, (memaddr >> (memwrite*8)), I2C_ACK_CHECK_EN) != ESP_OK) {ret=3; goto error;};
    }

    // send data
    if (i2c_master_write(cmd, data, len, I2C_ACK_CHECK_EN) != ESP_OK) {ret=5; goto error;};
    if (stop) {
        if (i2c_master_stop(cmd) != ESP_OK) {ret=6; goto error;};
    }

    ret = i2c_master_cmd_begin(i2c_obj->bus_id, cmd, (5000 + (1000 * len)) / portTICK_RATE_MS);

error:
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
    	char errs[32];
    	sprintf(errs, "I2C bus error (%d)", ret);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, errs));
    }
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
STATIC void mp_i2c_master_read(mp_machine_i2c_obj_t *i2c_obj, uint16_t slave_addr, uint8_t memread, uint32_t memaddr, uint8_t *data, uint16_t len, bool stop)
{
	esp_err_t ret = ESP_FAIL;

	memset(data, 0xFF, len);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK) {ret=1; goto error;};

    if (memread) {
    	// send slave address
        if (i2c_master_write_byte(cmd, ( slave_addr << 1 ) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN) != ESP_OK) {ret=2; goto error;};

        if (memread > 0) {
			// send memory address, MSByte first
			while (memread > 0) {
				// send memory address, MSByte first
				memread--;
				if (i2c_master_write_byte(cmd, (memaddr >> (memread*8)), I2C_ACK_CHECK_EN) != ESP_OK) {ret=3; goto error;};
			}

			if (stop) {
				// Finish the write transaction
			    if (i2c_master_stop(cmd) != ESP_OK) {ret=5; goto error;};
			    if (i2c_master_cmd_begin(i2c_obj->bus_id, cmd, 100 / portTICK_RATE_MS) != ESP_OK) {ret=6; goto error;};
			    i2c_cmd_link_delete(cmd);
			    // Start the read transaction
			    cmd = i2c_cmd_link_create();
			    if (i2c_master_start(cmd) != ESP_OK) {ret=7; goto error;};
			}
			else {
				// repeated start, generate START signal, slave address will be send next
				if (i2c_master_start(cmd) != ESP_OK) {ret=4; goto error;};
			}
        }
    }

	// READ, send slave address
    if (i2c_master_write_byte(cmd, ( slave_addr << 1 ) | I2C_MASTER_READ, I2C_ACK_CHECK_EN) != ESP_OK) {ret=8; goto error;};

    if (len > 1) {
        if (i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK) != ESP_OK) {ret=9; goto error;};
    }

    if (i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK) != ESP_OK) {ret=10; goto error;};
    if (i2c_master_stop(cmd) != ESP_OK) {ret=11; goto error;};

    ret = i2c_master_cmd_begin(i2c_obj->bus_id, cmd, (5000 + (1000 * len)) / portTICK_RATE_MS);

error:
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
    	char errs[32];
    	sprintf(errs, "I2C bus error (%d)", ret);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, errs));
    }
}

//----------------------------------------------------------------------------------
STATIC bool hw_i2c_slave_detect (mp_machine_i2c_obj_t *i2c_obj, uint16_t slave_addr)
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


// ============================================================================================
// === I2C MicroPython bindings ===============================================================
// ============================================================================================

enum { ARG_id, ARG_mode, ARG_speed, ARG_freq, ARG_sda, ARG_scl, ARG_slaveaddr, ARG_slavebuflen };

//----------------------------------------------------------
STATIC const mp_arg_t mp_machine_i2c_init_allowed_args[] = {
		{ MP_QSTR_id,                                                    MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_mode,				    	                             MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_speed,			MP_ARG_KW_ONLY                     | MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_freq,				MP_ARG_KW_ONLY                     | MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_sda,				MP_ARG_KW_ONLY  | MP_ARG_REQUIRED  | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
		{ MP_QSTR_scl,				MP_ARG_KW_ONLY  | MP_ARG_REQUIRED  | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
		{ MP_QSTR_slave_addr,		MP_ARG_KW_ONLY                     | MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_slave_bufflen,	MP_ARG_KW_ONLY                     | MP_ARG_INT, {.u_int = -1} },
};

//---------------------------------------------------------------------------------------
STATIC void _sched_callback(mp_obj_t function, int cmd, int addr, int len, uint8_t *sarg)
{
	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
	if (carg == NULL) return;
	if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_INT, cmd, NULL, NULL)) return;
	if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_INT, addr, NULL, NULL)) return;
	if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_INT, len, NULL, NULL)) return;
	if (sarg) {
		if (!make_carg_entry(carg, 3, MP_SCHED_ENTRY_TYPE_BYTES, len, sarg, NULL)) return;
	}
	else {
		if (!make_carg_entry(carg, 3, MP_SCHED_ENTRY_TYPE_NONE, 0, NULL, NULL)) return;
	}
	mp_sched_schedule(function, mp_const_none, carg);
}

//--------------------------------------------
STATIC void i2c_slave_task(void *pvParameters)
{
	mp_machine_i2c_obj_t *i2c_obj = (mp_machine_i2c_obj_t *)pvParameters;

	int rd_len;
	uint8_t cmd;
	int16_t mem_addr, mem_len;
	uint32_t trans_size = 0;
	int notify_res = 0;
	if (i2c_obj->slave_data == NULL) goto exit;

	if (i2c_slave_add_task(i2c_obj->bus_id, &i2c_slave_task_handle[i2c_obj->bus_id]) != ESP_OK) goto exit;

    while (1) {
    	// === Wait for transaction data ===
    	trans_size = 0;
    	notify_res = xTaskNotifyWait(0, ULONG_MAX, &trans_size, portMAX_DELAY);
    	if ((notify_res != pdPASS) || (trans_size < 1)) continue;

    	// === Check if the i2c object is still initialized ===
    	if (slave_mutex) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);
    	if (i2c_obj->bus_id < 0) {
        	if (slave_mutex) xSemaphoreGive(slave_mutex);
    		break;
    	}
    	if (i2c_obj->slave_data == NULL) {
        	if (slave_mutex) xSemaphoreGive(slave_mutex);
    		break;
    	}
    	if (slave_mutex) xSemaphoreGive(slave_mutex);

    	// Allocate the transaction data buffer
    	uint8_t *data = malloc(trans_size);
		if (data) {
			// Read the transaction data
			rd_len = i2c_slave_read_buffer(i2c_obj->bus_id, data, trans_size, 0);
			if (rd_len > 0) {
				if (slave_mutex) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);
				// --- Analyze the received data based on first byte received ---
				cmd = data[0] & 0xF0;	// 4-bit command id

				if ((cmd == 0x00) && (rd_len == 4)) {
					// READ data from buffer, 4 bytes received are buffer address & read length
					mem_addr = ((data[0] << 8) | data[1]) & 0x0FFF;	// 12-bit address
					mem_len = ((data[2] << 8) | data[3]) & 0x0FFF;	// 12-bit length
					if ((mem_addr + mem_len) > i2c_obj->slave_buflen) mem_len = i2c_obj->slave_buflen - mem_addr;
					if ((mem_addr < i2c_obj->slave_buflen) && (mem_len > 0)) {
						// Put the response data into i2c transmit buffer
						i2c_slave_write_buffer(i2c_obj->bus_id, i2c_obj->slave_data + mem_addr, mem_len, 500 / portTICK_PERIOD_MS);
						if (i2c_obj->slave_cb_read) {
							_sched_callback(i2c_obj->slave_cb_read, cmd>>4, mem_addr, mem_len, i2c_obj->slave_data + mem_addr);
						}
					}
				}
				else if ((cmd == 0x10) && (rd_len > 2)) {
					// WRITE data to buffer, first 2 bytes are buffer address
					mem_addr = ((data[0] << 8) | data[1]) & 0x0FFF;	// 12-bit address
					mem_len = rd_len - 2;
					if ((mem_addr + mem_len) > i2c_obj->slave_buflen) mem_len = i2c_obj->slave_buflen - mem_addr;
					if ((mem_addr < i2c_obj->slave_buflen) && (mem_len > 0)) {
						// Save the received data to the i2c data buffer
						memcpy(i2c_obj->slave_data + mem_addr, data + 2, mem_len);
						if (i2c_obj->slave_cb_write) {
							_sched_callback(i2c_obj->slave_cb_write, cmd>>4, mem_addr, mem_len, i2c_obj->slave_data + mem_addr);
						}
					}
				}
				else if ((cmd >= 0x20) && (cmd <= 0xF0)) {
					// COMMAND, optionally with DATA to pass to the callback function
					if (i2c_obj->slave_cb_data) {
						uint8_t *dataptr = NULL;
						if (rd_len > 1) dataptr = data+1;
						_sched_callback(i2c_obj->slave_cb_data, data[0], trans_size, rd_len-1, dataptr);
					}
				}
				if (slave_mutex) xSemaphoreGive(slave_mutex);
			}
			free(data);
		}
	}
exit:
	i2c_slave_remove_task(i2c_obj->bus_id);
    i2c_slave_task_handle[i2c_obj->bus_id] = NULL;
	vTaskDelete(NULL);
}

//----------------------------------
STATIC void _checkAddr(uint8_t addr)
{
	if ((addr < 0x08) || (addr > 0x77)) {
        mp_raise_ValueError("Wrong i2c address (0x08 - 0x77 allowed)");
	}
}

//--------------------------------------------------
STATIC void _checkMaster(mp_machine_i2c_obj_t *self)
{
	if (self->mode != I2C_MODE_MASTER) {
        mp_raise_ValueError("I2C not in MASTER mode)");
	}
}

//-------------------------------------------------
STATIC void _checkSlave(mp_machine_i2c_obj_t *self)
{
	if (self->mode != I2C_MODE_SLAVE) {
        mp_raise_ValueError("I2C not in SLAVE mode)");
	}
}

//-----------------------------------------------------------------------------------------------
STATIC void mp_machine_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    mp_machine_i2c_obj_t *self = self_in;
    if (i2c_used[self->bus_id] >= 0) {
		if (self->mode == I2C_MODE_MASTER)
			mp_printf(print, "I2C (Port=%u, Mode=MASTER, Speed=%u Hz, sda=%d, scl=%d)", self->bus_id, self->speed, self->sda, self->scl);
		else {
			mp_printf(print, "I2C (Port=%u, Mode=SLAVE, Speed=%u Hz, sda=%d, scl=%d, addr=%d, buffer=%d B)",
					self->bus_id, self->speed, self->sda, self->scl, self->slave_addr, self->slave_buflen);
			mp_printf(print, "\n     ReadCB=%s, WriteCB=%s, DataCB=%s",
					self->slave_cb_read ? "True" : "False", self->slave_cb_write ? "True" : "False", self->slave_cb_data ? "True" : "False");
		    if (i2c_slave_task_handle[self->bus_id]) {
		    	mp_printf(print, "\n     I2C task minimum free stack: %u", uxTaskGetStackHighWaterMark(i2c_slave_task_handle[self->bus_id]));
		    }
		}
    }
    else {
		mp_printf(print, "I2C (Deinitialized)");
    }
}

//---------------------------------------------------------------------------------------------------------------
mp_obj_t mp_machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	mp_arg_val_t args[MP_ARRAY_SIZE(mp_machine_i2c_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(mp_machine_i2c_init_allowed_args), mp_machine_i2c_init_allowed_args, args);

    int8_t sda;
    int8_t scl;
    int32_t speed;
    int8_t bus_id = args[ARG_id].u_int;
    if (bus_id < 0) bus_id = I2C_NUM_0;

    if (args[ARG_freq].u_int > 0) speed = args[ARG_freq].u_int;
    else speed = args[ARG_speed].u_int;
    if (speed < 0) speed = 100000;

    // Check the peripheral id
    if (bus_id < 0 || bus_id > 1) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus not available"));
    }
    if (i2c_used[bus_id] >= 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus already used"));
    }
    // Check mode
    int mode = args[ARG_mode].u_int;
    if (mode < 0) mode = I2C_MODE_MASTER;
    if ((mode != I2C_MODE_MASTER) && (mode != I2C_MODE_SLAVE)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "MASTER or SLAVE mode must be selected"));
    }

    sda = machine_pin_get_gpio(args[ARG_sda].u_obj);
    scl = machine_pin_get_gpio(args[ARG_scl].u_obj);

    // Create I2C object
	mp_machine_i2c_obj_t *self = m_new_obj(mp_machine_i2c_obj_t );
	self->base.type = &machine_hw_i2c_type;
	self->mode = mode;
	self->bus_id = bus_id;
	self->speed = speed;
	self->scl = scl;
	self->sda = sda;
	self->cmd = NULL;
	self->rx_buflen = 0;
	self->rx_bufidx = 0;
	self->rx_data = NULL;
	self->slave_buflen = 0;
	self->slave_addr = 0;
	self->slave_data = NULL;
	self->slave_cb_read = NULL;
	self->slave_cb_write = NULL;
	self->slave_cb_data = NULL;

	if (mode == I2C_MODE_MASTER) {
		// Setup I2C master
		if (i2c_init_master(self) != ESP_OK) {
	        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
		}
    }
    else {
    	if (slave_mutex == NULL) {
    		slave_mutex = xSemaphoreCreateMutex();
    	}
		// Set I2C slave address
    	if (args[ARG_slaveaddr].u_int > 0) {
    		_checkAddr(args[ARG_slaveaddr].u_int);
        	self->slave_addr = args[ARG_slaveaddr].u_int;
    	}
    	else self->slave_addr = I2C_SLAVE_ADDR_DEFAULT;

    	// Set I2C slave buffers
    	if ((args[ARG_slavebuflen].u_int > I2C_SLAVE_DEFAULT_BUFF_LEN) && (args[ARG_slavebuflen].u_int <= I2C_SLAVE_MAX_BUFF_LEN))
    		self->slave_buflen = args[ARG_slavebuflen].u_int;
    	else self->slave_buflen = I2C_SLAVE_DEFAULT_BUFF_LEN;

    	self->slave_data = malloc(self->slave_buflen);
    	if (self->slave_data == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating slave data buffer"));
    	}
    	memset(self->slave_data, 0, self->slave_buflen);

    	if (i2c_init_slave(self) != ESP_OK) {
    		free(self->slave_data);
    		self->slave_data = NULL;
	        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
    	}

    	if (i2c_slave_task_handle[self->bus_id] == NULL) {
			#if CONFIG_MICROPY_USE_BOTH_CORES
    		xTaskCreate(i2c_slave_task, "i2c_slave_task", 1024, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle[self->bus_id]);
			#else
    		xTaskCreatePinnedToCore(i2c_slave_task, "i2c_slave_task", 1024, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle[self->bus_id], MainTaskCore);
			#endif
    	}
    }

	i2c_used[bus_id] = mode;

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_init(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    mp_machine_i2c_obj_t *self = pos_args[0];

	mp_arg_val_t args[MP_ARRAY_SIZE(mp_machine_i2c_init_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mp_machine_i2c_init_allowed_args), mp_machine_i2c_init_allowed_args, args);

    int8_t sda;
    int8_t scl;
    int8_t slave_addr;
    int32_t speed;
    uint8_t changed = 0;

    int8_t old_bus_id = self->bus_id;
    int8_t bus_id = args[ARG_id].u_int;
    if (bus_id < 0) bus_id = self->bus_id;

    if (args[ARG_freq].u_int > 0) speed = args[ARG_freq].u_int;
    else speed = args[ARG_speed].u_int;
    if (speed < 0) speed = self->speed;

    // Check the peripheral id
    if (bus_id < 0 || bus_id > 1) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus not available"));
    }
    // Check mode
    int mode = args[ARG_mode].u_int;
    if ((mode >= 0) && (mode != self->mode)) {
		if ((mode != I2C_MODE_MASTER) && (mode != I2C_MODE_SLAVE)) {
			nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "MASTER or SLAVE mode must be selected"));
		}
		changed++;
    }
    else mode = self->mode;

    scl = self->scl;
    sda = self->sda;

    if (args[ARG_sda].u_obj != MP_OBJ_NULL) {
        sda = machine_pin_get_gpio(args[ARG_sda].u_obj);
    }
    if (args[ARG_scl].u_obj != MP_OBJ_NULL) {
        scl = machine_pin_get_gpio(args[ARG_scl].u_obj);
    }

    // Check if the configuration changed
    if (old_bus_id != bus_id) changed++;
    if (self->speed != speed) changed++;
    if (self->scl != scl) changed++;
    if (self->sda != sda) changed++;

    if (args[ARG_slaveaddr].u_int > 0) {
		_checkAddr(args[ARG_slaveaddr].u_int);
		slave_addr = args[ARG_slaveaddr].u_int;
    	changed++;
	}
	else slave_addr = self->slave_addr;

    if (changed) {
    	if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex)) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);
    	if (i2c_used[old_bus_id] >= 0) {
    		i2c_driver_delete(old_bus_id);
    		i2c_used[old_bus_id] = -1;
    	}
        if (i2c_used[bus_id] >= 0) {
        	if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex)) xSemaphoreGive(slave_mutex);
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus already used"));
        }

    	self->bus_id = bus_id;
		self->speed = speed;
		self->scl = scl;
		self->sda = sda;
		if (mode == I2C_MODE_MASTER) {
			if (i2c_init_master(self) != ESP_OK) {
		        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
			}
		}
	    else {
			// Setup I2C slave
	    	self->slave_addr = slave_addr;
	    	if (i2c_init_slave(self) != ESP_OK) {
	    		if (self->slave_data) free(self->slave_data);
	    		self->slave_data = NULL;
	        	if (slave_mutex) xSemaphoreGive(slave_mutex);
		        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
	    	}
	    	if (i2c_slave_task_handle[self->bus_id] == NULL) {
				#if CONFIG_MICROPY_USE_BOTH_CORES
				xTaskCreate(i2c_slave_task, "i2c_slave_task", 1024, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle[self->bus_id]);
				#else
				xTaskCreatePinnedToCore(i2c_slave_task, "i2c_slave_task", 1024, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle[self->bus_id], MainTaskCore);
				#endif
	    	}
	    }
		i2c_used[bus_id] = mode;
    	if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex)) xSemaphoreGive(slave_mutex);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_init_obj, 1, mp_machine_i2c_init);

//---------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_scan(mp_obj_t self_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkMaster(self);

    mp_obj_t list = mp_obj_new_list(0, NULL);

    // don't include in scan the reserved 7-bit addresses: 0x00-0x07 & 0x78-0x7F

	for (int addr = 0x08; addr < 0x78; ++addr) {
		if (hw_i2c_slave_detect(self, addr)) {
			mp_obj_list_append(list, MP_OBJ_NEW_SMALL_INT(addr));
		}
	}
    return list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_i2c_scan_obj, mp_machine_i2c_scan);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_readfrom(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    STATIC const mp_arg_t machine_i2c_readfrom_args[] = {
        { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_nbytes,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
    };

    mp_machine_i2c_obj_t *self = pos_args[0];
    _checkMaster(self);

    // parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_readfrom_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(args), machine_i2c_readfrom_args, args);

    _checkAddr(args[0].u_int);

    vstr_t vstr;
    vstr_init_len(&vstr, args[1].u_int);
    mp_i2c_master_read(self, args[0].u_int, false, 0, (uint8_t*)vstr.buf, vstr.len, false);

    // Return read data as string
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_readfrom_obj, 1, mp_machine_i2c_readfrom);

//---------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_readfrom_into(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    mp_machine_i2c_obj_t *self = pos_args[0];
    _checkMaster(self);

    STATIC const mp_arg_t machine_i2c_readfrom_into_args[] = {
        { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
        { MP_QSTR_buf,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    // parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_readfrom_into_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(args), machine_i2c_readfrom_into_args, args);

    _checkAddr(args[0].u_int);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1].u_obj, &bufinfo, MP_BUFFER_WRITE);

    mp_i2c_master_read(self, args[0].u_int, false, 0, bufinfo.buf, bufinfo.len, false);

    // Return read length
    return mp_obj_new_int(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_readfrom_into_obj, 1, mp_machine_i2c_readfrom_into);

//---------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_writeto(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    mp_machine_i2c_obj_t *self = pos_args[0];
    _checkMaster(self);

    STATIC const mp_arg_t machine_i2c_writeto_args[] = {
        { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_buf,     MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_stop,    MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = true} },
    };

    // parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_writeto_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(args), machine_i2c_writeto_args, args);

    _checkAddr(args[0].u_int);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1].u_obj, &bufinfo, MP_BUFFER_READ);

    mp_i2c_master_write(self, args[0].u_int, 0, 0, bufinfo.buf, bufinfo.len, args[2].u_bool);

    return mp_obj_new_int(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_writeto_obj, 1, mp_machine_i2c_writeto);


//---------------------------------------------------------
STATIC const mp_arg_t mp_machine_i2c_mem_allowed_args[] = {
    { MP_QSTR_addr,			MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
    { MP_QSTR_memaddr,		MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
    { MP_QSTR_arg,			MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_adrlen,		MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 1} },
    { MP_QSTR_stop,			MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = true} },
};

//---------------------------------------------
uint8_t getMemAdrLen(int memlen, uint32_t addr)
{
	if ((memlen < 1) || (memlen > 4)) {
		mp_raise_ValueError("Memory address length error, 1 - 4 allowed");
	}
	uint8_t len = 1;
	if (addr > 0xFF) len++;
	if (addr > 0xFFFF) len++;
	if (addr > 0xFFFFFF) len++;
	if (memlen > len) len = memlen;
	return len;
}

//-----------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_readfrom_mem(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_addr, ARG_memaddr, ARG_n, ARG_memlen, ARG_stop };
    mp_machine_i2c_obj_t *self = pos_args[0];
    _checkMaster(self);

    // parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(mp_machine_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(mp_machine_i2c_mem_allowed_args), mp_machine_i2c_mem_allowed_args, args);

    _checkAddr(args[ARG_addr].u_int);

    // Get read length
    int n = mp_obj_get_int(args[ARG_n].u_obj);
    if (n > 0) {
    	uint32_t addr = (uint32_t)mp_obj_get_int64(args[ARG_memaddr].u_obj);
    	uint8_t memlen = getMemAdrLen(args[ARG_memlen].u_int, addr);
        // Create the data output buffer
        vstr_t vstr;
        vstr_init_len(&vstr, n);

        // Transfer data
        mp_i2c_master_read(self, args[ARG_addr].u_int, memlen, addr, (uint8_t *)vstr.buf, vstr.len, args[ARG_stop].u_bool);
        // Return read data as string
        return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
    }
    // Return empty string
    return mp_const_empty_bytes;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_readfrom_mem_obj, 1, mp_machine_i2c_readfrom_mem);

//----------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_readfrom_mem_into(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_addr, ARG_memaddr, ARG_buf, ARG_memlen, ARG_stop };
    mp_machine_i2c_obj_t *self = pos_args[0];
    _checkMaster(self);

    // parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(mp_machine_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(mp_machine_i2c_mem_allowed_args), mp_machine_i2c_mem_allowed_args, args);

    _checkAddr(args[ARG_addr].u_int);

    // Get the output data buffer
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_buf].u_obj, &bufinfo, MP_BUFFER_WRITE);

    if (bufinfo.len > 0) {
    	uint32_t addr = (uint32_t)mp_obj_get_int64(args[ARG_memaddr].u_obj);
    	uint8_t memlen = getMemAdrLen(args[ARG_memlen].u_int, addr);
        // Transfer data into buffer
        mp_i2c_master_read(self, args[ARG_addr].u_int, memlen, addr, bufinfo.buf, bufinfo.len, args[ARG_stop].u_bool);
    }
    return mp_obj_new_int(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_readfrom_mem_into_obj, 1, mp_machine_i2c_readfrom_mem_into);

//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_writeto_mem(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_addr, ARG_memaddr, ARG_buf, ARG_memlen };
    mp_machine_i2c_obj_t *self = pos_args[0];
    _checkMaster(self);

    // parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(mp_machine_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(mp_machine_i2c_mem_allowed_args), mp_machine_i2c_mem_allowed_args, args);

    _checkAddr(args[ARG_addr].u_int);

    // Get the input data buffer
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_buf].u_obj, &bufinfo, MP_BUFFER_READ);

	uint32_t addr = (uint32_t)mp_obj_get_int64(args[ARG_memaddr].u_obj);
	uint8_t memlen = getMemAdrLen(args[ARG_memlen].u_int, addr);
    // Transfer data from buffer
    mp_i2c_master_write(self, args[ARG_addr].u_int, memlen, addr, bufinfo.buf, bufinfo.len, true);

    return mp_obj_new_int(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_writeto_mem_obj, 1, mp_machine_i2c_writeto_mem);

//-------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_deinit(mp_obj_t self_in) {
    mp_machine_i2c_obj_t *self = self_in;

    if (i2c_used[self->bus_id] >= 0) {
    	if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex)) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);

    	i2c_driver_delete(self->bus_id);
        i2c_used[self->bus_id] = -1;
        if (self->slave_data) free(self->slave_data);
		self->slave_data = NULL;

        if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex)) xSemaphoreGive(slave_mutex);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_i2c_deinit_obj, mp_machine_i2c_deinit);


// ============================================================================================
// ==== Low level MicroPython I2C commands ====================================================
// ============================================================================================

//-------------------------------------------------
static void _checkBegin(mp_machine_i2c_obj_t *self)
{
    _checkMaster(self);
    if (self->cmd == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C: command before begin!"));
    }
}

// Begin i2c transaction
//-------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_begin(mp_obj_t self_in, mp_obj_t rxlen_in) {
    mp_machine_i2c_obj_t *self = self_in;
    _checkMaster(self);

    int rxbuflen = mp_obj_get_int(rxlen_in);
    if ((rxbuflen < 0) || (rxbuflen > I2C_RX_MAX_BUFF_LEN)) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Buffer length error"));
    }

    if (self->cmd != NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C: 2nd begin before end not allowed!"));
    }

    if (self->rx_data) free(self->rx_data);
    self->rx_data = NULL;
    if (rxbuflen) {
		self->rx_data = malloc(rxbuflen);
		if (self->rx_data == NULL) {
			nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating rx buffer"));
		}
    }
    self->rx_buflen = rxbuflen;
    self->rx_bufidx = 0;

    self->cmd = i2c_cmd_link_create();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_i2c_begin_obj, mp_machine_i2c_begin);

// Queue start command
//------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_start(mp_obj_t self_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    // Queue start command
    if (i2c_master_start(self->cmd) != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Start command error"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_i2c_start_obj, mp_machine_i2c_start);

// Queue slave address
//------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_address(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t rw_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    uint8_t slave_addr = mp_obj_get_int(addr_in);
    uint8_t rw = mp_obj_get_int(rw_in) & 1; // 0 -> write, 1 -> read

    _checkAddr(slave_addr);

    // Queue slave address
    if (i2c_master_write_byte(self->cmd, (slave_addr << 1) | rw, I2C_ACK_CHECK_EN) != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Address write error"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_i2c_address_obj, mp_machine_i2c_address);

// Queue stop command
//-----------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_stop(mp_obj_t self_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    if (i2c_master_stop(self->cmd) != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Stop command error"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_i2c_stop_obj, mp_machine_i2c_stop);

//----------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_write_byte(mp_obj_t self_in, mp_obj_t val_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    uint8_t val = mp_obj_get_int(val_in);

    if (i2c_master_write_byte(self->cmd, val, I2C_ACK_CHECK_EN) != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Write error"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_i2c_write_byte_obj, mp_machine_i2c_write_byte);

//-----------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_write_bytes(mp_obj_t self_in, mp_obj_t buf_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    if (i2c_master_write(self->cmd, bufinfo.buf, bufinfo.len, I2C_ACK_CHECK_EN) != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Write error"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_i2c_write_bytes_obj, mp_machine_i2c_write_bytes);

//----------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_read_byte(mp_obj_t self_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    if ((self->rx_data == NULL) || ((self->rx_bufidx + 1) >= self->rx_buflen)) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Rx buffer overflow"));
    }

    if (i2c_master_read_byte(self->cmd, &(self->rx_data[self->rx_bufidx]), I2C_MASTER_NACK) != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Read error"));
    }
    self->rx_bufidx++;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_i2c_read_byte_obj, mp_machine_i2c_read_byte);

//----------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_read_bytes(mp_obj_t self_in, mp_obj_t len_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    esp_err_t ret = ESP_OK;
    int len = mp_obj_get_int(len_in);
    if ((len < 1) || (len > self->rx_buflen)) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Rx buffer overflow"));
    }
    if ((self->rx_data == NULL) || ((self->rx_bufidx + len) >= self->rx_buflen)) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Rx buffer overflow"));
    }

    if (len > 1) {
        ret = i2c_master_read(self->cmd, &(self->rx_data[self->rx_bufidx]), len-1, I2C_MASTER_ACK);
    }
    if (ret == ESP_OK) ret = i2c_master_read_byte(self->cmd, &(self->rx_data[self->rx_bufidx + len - 1]), I2C_MASTER_NACK);

    if (ret != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Read error"));
    }
    self->rx_bufidx += len;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_i2c_read_bytes_obj, mp_machine_i2c_read_bytes);

// End i2c transaction
//----------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_end(mp_obj_t self_in) {
    mp_machine_i2c_obj_t *self = self_in;

    _checkBegin(self);

    esp_err_t res = i2c_master_cmd_begin(self->bus_id, self->cmd, (5000 / portTICK_RATE_MS));

    i2c_cmd_link_delete(self->cmd);
    self->cmd = NULL;

    mp_obj_t ret = mp_const_none;
    if ((res == ESP_OK) && (self->rx_data) && (self->rx_bufidx > 0)) {
        vstr_t vstr;
        vstr_init_len(&vstr, self->rx_bufidx);
        memcpy(vstr.buf, self->rx_data, self->rx_bufidx);
        // Return read data as string
        ret = mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
    }

    self->rx_buflen = 0;
    self->rx_bufidx = 0;
    if (self->rx_data) free(self->rx_data);
    self->rx_data = NULL;

    if (res != ESP_OK) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "I2C bus error"));
    }
    return ret;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_i2c_end_obj, mp_machine_i2c_end);


// ============================================================================================
// ==== I2C slave functions ===================================================================
// ============================================================================================


//---------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_write(mp_obj_t self_in, mp_obj_t buf_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

	if (slave_mutex) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);

	int res = i2c_slave_write_buffer(self->bus_id, bufinfo.buf, bufinfo.len, 500 / portTICK_PERIOD_MS);

	if (slave_mutex) xSemaphoreGive(slave_mutex);

	if (res < 0) return mp_const_false;
    return mp_obj_new_int(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_i2c_slave_write_obj, mp_machine_i2c_slave_write);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_setdata(mp_obj_t self_in, mp_obj_t buf_in, mp_obj_t addr_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    int addr = mp_obj_get_int(addr_in);

    if (addr >= self->slave_buflen) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Address not in slave data buffer"));
    }

    if ((addr + bufinfo.len) > self->slave_buflen) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Data does not fit into slave data buffer"));
	}

	if (slave_mutex) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);

	memcpy(self->slave_data + addr, bufinfo.buf, bufinfo.len);

	if (slave_mutex) xSemaphoreGive(slave_mutex);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_i2c_slave_setdata_obj, mp_machine_i2c_slave_setdata);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_getdata(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t len_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    int addr = mp_obj_get_int(addr_in);
    int len = mp_obj_get_int(len_in);

    if (addr >= self->slave_buflen) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Address not in slave data buffer"));
    }

    if ((addr + len) > self->slave_buflen) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Requested data outside buffer"));
	}

	if (slave_mutex) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);

    mp_obj_t data = mp_obj_new_bytes(self->slave_data + addr, len);

	if (slave_mutex) xSemaphoreGive(slave_mutex);

    // Return read data as byte array
    return data;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_i2c_slave_getdata_obj, mp_machine_i2c_slave_getdata);

//----------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_callback(mp_obj_t self_in, mp_obj_t type_in, mp_obj_t func)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    if ((!MP_OBJ_IS_FUN(func)) && (!MP_OBJ_IS_METH(func)) && (func != mp_const_none)) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Function argument required"));
    }
    int ftype = mp_obj_get_int(type_in);
    if ((ftype < 1) || (ftype > 3)) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Wrong callback type"));
    }

	if (slave_mutex) xSemaphoreTake(slave_mutex, I2C_SLAVE_MUTEX_TIMEOUT);

	if (func == mp_const_none) {
		if (ftype == 1) self->slave_cb_read = NULL;
		else if (ftype == 2) self->slave_cb_write = NULL;
		else if (ftype == 3) self->slave_cb_data = NULL;
	}
	else {
		if (ftype == 1) self->slave_cb_read = func;
		else if (ftype == 2) self->slave_cb_write = func;
		else if (ftype == 3) self->slave_cb_data = func;
	}

	if (slave_mutex) xSemaphoreGive(slave_mutex);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_i2c_slave_callback_obj, mp_machine_i2c_slave_callback);


//===================================================================
STATIC const mp_rom_map_elem_t mp_machine_i2c_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),                (mp_obj_t)&mp_machine_i2c_init_obj },
    { MP_ROM_QSTR(MP_QSTR_deinit),              (mp_obj_t)&mp_machine_i2c_deinit_obj },
    { MP_ROM_QSTR(MP_QSTR_scan),                (mp_obj_t)&mp_machine_i2c_scan_obj },

    // Standard methods
    { MP_ROM_QSTR(MP_QSTR_readfrom),            (mp_obj_t)&mp_machine_i2c_readfrom_obj },
    { MP_ROM_QSTR(MP_QSTR_readfrom_into),       (mp_obj_t)&mp_machine_i2c_readfrom_into_obj },
    { MP_ROM_QSTR(MP_QSTR_writeto),             (mp_obj_t)&mp_machine_i2c_writeto_obj },
    { MP_ROM_QSTR(MP_QSTR_setdata),       		(mp_obj_t)&mp_machine_i2c_slave_setdata_obj },
    { MP_ROM_QSTR(MP_QSTR_getdata),       		(mp_obj_t)&mp_machine_i2c_slave_getdata_obj },
    { MP_ROM_QSTR(MP_QSTR_slavewrite),    		(mp_obj_t)&mp_machine_i2c_slave_write_obj },
    { MP_ROM_QSTR(MP_QSTR_callback),            (mp_obj_t)&mp_machine_i2c_slave_callback_obj },

    // Memory methods
    { MP_ROM_QSTR(MP_QSTR_readfrom_mem),		(mp_obj_t)&mp_machine_i2c_readfrom_mem_obj },
    { MP_ROM_QSTR(MP_QSTR_readfrom_mem_into),   (mp_obj_t)&mp_machine_i2c_readfrom_mem_into_obj },
    { MP_ROM_QSTR(MP_QSTR_writeto_mem),         (mp_obj_t)&mp_machine_i2c_writeto_mem_obj },

	// Low level MicroPython I2C methods
    { MP_ROM_QSTR(MP_QSTR_begin),               (mp_obj_t)&mp_machine_i2c_begin_obj },
    { MP_ROM_QSTR(MP_QSTR_start),               (mp_obj_t)&mp_machine_i2c_start_obj },
    { MP_ROM_QSTR(MP_QSTR_address),             (mp_obj_t)&mp_machine_i2c_address_obj },
    { MP_ROM_QSTR(MP_QSTR_stop),                (mp_obj_t)&mp_machine_i2c_stop_obj },
    { MP_ROM_QSTR(MP_QSTR_read_byte),           (mp_obj_t)&mp_machine_i2c_read_byte_obj },
    { MP_ROM_QSTR(MP_QSTR_read_bytes),          (mp_obj_t)&mp_machine_i2c_read_bytes_obj },
    { MP_ROM_QSTR(MP_QSTR_write_byte),          (mp_obj_t)&mp_machine_i2c_write_byte_obj },
    { MP_ROM_QSTR(MP_QSTR_write_bytes),         (mp_obj_t)&mp_machine_i2c_write_bytes_obj },
    { MP_ROM_QSTR(MP_QSTR_end),                 (mp_obj_t)&mp_machine_i2c_end_obj },

	// Constants
    { MP_OBJ_NEW_QSTR(MP_QSTR_MASTER),          MP_OBJ_NEW_SMALL_INT(I2C_MODE_MASTER) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_SLAVE),           MP_OBJ_NEW_SMALL_INT(I2C_MODE_SLAVE) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_READ),            MP_OBJ_NEW_SMALL_INT(I2C_MASTER_READ) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_WRITE),           MP_OBJ_NEW_SMALL_INT(I2C_MASTER_WRITE) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_CB_READ),         MP_OBJ_NEW_SMALL_INT(1) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_CB_WRITE),        MP_OBJ_NEW_SMALL_INT(2) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_CB_DATA),         MP_OBJ_NEW_SMALL_INT(3) },
};

STATIC MP_DEFINE_CONST_DICT(mp_machine_i2c_locals_dict, mp_machine_i2c_locals_dict_table);

//=========================================
const mp_obj_type_t machine_hw_i2c_type = {
    { &mp_type_type },
    .name = MP_QSTR_I2C,
    .print = mp_machine_i2c_print,
    .make_new = mp_machine_i2c_make_new,
    .locals_dict = (mp_obj_dict_t*)&mp_machine_i2c_locals_dict,
};
