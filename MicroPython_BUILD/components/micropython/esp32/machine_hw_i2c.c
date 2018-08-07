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

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "esp_log.h"

#include "py/mpstate.h"
#include "py/runtime.h"
#include "py/obj.h"

#include "modmachine.h"

#define I2C_ACK_CHECK_EN            (1)
#define I2C_RX_MAX_BUFF_LEN			2048	// maximum low level commands receive buffer length
#define I2C_SLAVE_DEFAULT_BUFF_LEN	256		// default slave buffer length
#define I2C_SLAVE_ADDR_DEFAULT		32		// default slave address
#define I2C_SLAVE_MUTEX_TIMEOUT		(500 / portTICK_PERIOD_MS)
#define I2C_SLAVE_TASK_STACK_SIZE   832

#define I2C_SLAVE_CBTYPE_NONE       0
#define I2C_SLAVE_CBTYPE_ADDR       1
#define I2C_SLAVE_CBTYPE_DATA_RX    2
#define I2C_SLAVE_CBTYPE_DATA_TX    4

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
    uint16_t slave_rolen;       // slave only, read only buffer area length
    uint32_t *slave_cb;	        // slave only, slave callback function
    bool slave_busy;
    uint8_t slave_cbtype;
} mp_machine_i2c_obj_t;


extern int MainTaskCore;

const mp_obj_type_t machine_hw_i2c_type;

static int i2c_used[I2C_MODE_MAX] = { -1, -1 };
static QueueHandle_t slave_mutex[I2C_MODE_MAX] = { NULL, NULL };
static TaskHandle_t i2c_slave_task_handle = NULL;
static i2c_slave_state_t slave_state;


// ============================================================================================
// === Low level I2C functions using esp-idf i2c-master driver ================================
// ============================================================================================

//--------------------------------------------------------------
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
    return i2c_driver_install(i2c_obj->bus_id, I2C_MODE_MASTER, 0, 0, false, ESP_INTR_FLAG_IRAM);
}

//------------------------------------------------------------------------
STATIC esp_err_t i2c_init_slave (mp_machine_i2c_obj_t *i2c_obj, bool busy)
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
    return i2c_driver_install(i2c_obj->bus_id, conf.mode, i2c_obj->slave_buflen, i2c_obj->slave_rolen, busy, ESP_INTR_FLAG_IRAM);
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------
STATIC int mp_i2c_master_write(mp_machine_i2c_obj_t *i2c_obj, uint16_t slave_addr, uint8_t memwrite, uint32_t memaddr, uint8_t *data, uint16_t len, bool stop)
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
    if ((data) && (len > 0)) {
        if (i2c_master_write(cmd, data, len, I2C_ACK_CHECK_EN) != ESP_OK) {ret=5; goto error;};
    }
    if (stop) {
        if (i2c_master_stop(cmd) != ESP_OK) {ret=6; goto error;};
    }

    ret = i2c_master_cmd_begin(i2c_obj->bus_id, cmd, (5000 + (1000 * len)) / portTICK_RATE_MS);

error:
    i2c_cmd_link_delete(cmd);

    return ret;
}

//----------------------------------------------------------------------------------------------------------------------------------------------------------
STATIC int mp_i2c_master_read(mp_machine_i2c_obj_t *i2c_obj, uint16_t slave_addr, uint8_t memread, uint32_t memaddr, uint8_t *data, uint16_t len, bool stop)
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

    return ret;
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

//---------------------------------------------------------------------------------------------------
STATIC void _sched_callback(mp_obj_t function, int cbtype, int addr, int len, int ovf, uint8_t *data)
{
    mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    if (carg == NULL) return;
    if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_INT, cbtype, NULL, NULL)) return;
    if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_INT, addr, NULL, NULL)) return;
    if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_INT, len, NULL, NULL)) return;
    if (!make_carg_entry(carg, 3, MP_SCHED_ENTRY_TYPE_INT, ovf, NULL, NULL)) return;
    if (data) {
        if (!make_carg_entry(carg, 4, MP_SCHED_ENTRY_TYPE_BYTES, len, data, NULL)) return;
    }
    else {
        if (!make_carg_entry(carg, 4, MP_SCHED_ENTRY_TYPE_NONE, 0, NULL, NULL)) return;
    }
    mp_sched_schedule(function, mp_const_none, carg);
}

//--------------------------------------------
STATIC void i2c_slave_task(void *pvParameters)
{
    mp_machine_i2c_obj_t *i2c_obj = (mp_machine_i2c_obj_t *)pvParameters;

    int len, ovf, rdlen, addr;
    uint8_t cb_type;
    uint32_t notify_val = 0;
    int notify_res = 0;
    uint8_t *data;

    if (i2c_obj->slave_buflen == 0) goto exit;

    if (i2c_slave_add_task(i2c_obj->bus_id, &i2c_slave_task_handle, &slave_state) != ESP_OK) goto exit;

    //ESP_LOGD("[I2C]", "Slave task started");
    while (1) {
        // === Wait for notification from I2C interrupt routine ===
        notify_val = 0;
        notify_res = xTaskNotifyWait(0, ULONG_MAX, &notify_val, 1000 / portTICK_RATE_MS);

        if (slave_mutex[i2c_obj->bus_id]) xSemaphoreTake(slave_mutex[i2c_obj->bus_id], I2C_SLAVE_MUTEX_TIMEOUT);
        if (notify_res != pdPASS) {
            if ((i2c_used[0] != I2C_MODE_SLAVE) && (i2c_used[1] != I2C_MODE_SLAVE)) {
                //ESP_LOGD("[I2C]", "all i2c slave instances deleted");
                if (slave_mutex[i2c_obj->bus_id]) xSemaphoreGive(slave_mutex[i2c_obj->bus_id]);
                break;
            }
            if (slave_mutex[i2c_obj->bus_id]) xSemaphoreGive(slave_mutex[i2c_obj->bus_id]);
            continue;
        }

        // *** notification received
        // Check if task exit requested or all i2c instances are deinitialized
        if (notify_val == I2C_SLAVE_DRIVER_DELETED) {
            //ESP_LOGD("[I2C]", "i2c driver deleted (%d)", i2c_obj->bus_id);
        }
        if ((notify_val == I2C_SLAVE_DRIVER_DELETED) && ((i2c_used[0] != I2C_MODE_SLAVE) && (i2c_used[1] != I2C_MODE_SLAVE))) {
            if (slave_mutex[i2c_obj->bus_id]) xSemaphoreGive(slave_mutex[i2c_obj->bus_id]);
            break;
        }
        if ((i2c_used[0] != I2C_MODE_SLAVE) && (i2c_used[1] != I2C_MODE_SLAVE)) {
            //ESP_LOGD("[I2C]", "all i2c slave instances deleted");
            if (slave_mutex[i2c_obj->bus_id]) xSemaphoreGive(slave_mutex[i2c_obj->bus_id]);
            break;
        }

        if ((slave_state.rxptr == 0) && (slave_state.txptr == 0)) {
            // address received from master
            cb_type = I2C_SLAVE_CBTYPE_ADDR;
            ovf = 0;
            len = 0;
            addr = slave_state.rxaddr;
        }
        else if (slave_state.status & 0x02) {
            // read transaction, data sent to master
            cb_type = I2C_SLAVE_CBTYPE_DATA_TX;
            ovf = slave_state.txovf;
            len = slave_state.txptr;
            addr = slave_state.txaddr;
        }
        else {
            // write transaction, data received from master
            cb_type = I2C_SLAVE_CBTYPE_DATA_RX;
            ovf = slave_state.rxovf;
            len = slave_state.rxptr;
            addr = slave_state.rxaddr;
        }
        cb_type &= i2c_obj->slave_cbtype; // mask allowed callback types

        if ((cb_type) && (i2c_obj->slave_cb)) {
            data = NULL;
            if (len > 0) {
               data = malloc(len);
               if (data) {
                   rdlen = i2c_slave_read_buffer(i2c_obj->bus_id, data, addr, len, I2C_SLAVE_MUTEX_TIMEOUT);
                   if (rdlen != len) {
                       free(data);
                       data = NULL;
                   }
               }
            }
            _sched_callback(i2c_obj->slave_cb, cb_type, addr, len, ovf, data);
            if (data) free(data);
        }
        if (slave_mutex[i2c_obj->bus_id]) xSemaphoreGive(slave_mutex[i2c_obj->bus_id]);
    }

exit:
    i2c_slave_remove_task(i2c_obj->bus_id);
    //ESP_LOGD("[I2C]", "Slave task deleted");
    i2c_slave_task_handle = NULL;
    vTaskDelete(NULL);
}


// ============================================================================================
// === I2C MicroPython bindings ===============================================================
// ============================================================================================

enum { ARG_id, ARG_mode, ARG_speed, ARG_freq, ARG_sda, ARG_scl, ARG_slaveaddr, ARG_slavebuflen, ARG_rolen, ARG_busy };

// Arguments for new object and init method
//----------------------------------------------------------
STATIC const mp_arg_t mp_machine_i2c_init_allowed_args[] = {
		{ MP_QSTR_id,                                MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_mode,				    	         MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_speed,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_freq,				MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_sda,				MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
		{ MP_QSTR_scl,				MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
		{ MP_QSTR_slave_addr,		MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
		{ MP_QSTR_slave_bufflen,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_slave_rolen,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_slave_busy,       MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
};

//----------------------------------------------
static uint8_t * get_buffer(void *buff, int len)
{
    if (len > 0) {
        uint8_t *buf = heap_caps_malloc(len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (buf == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating I2C data buffer"));
        }
        memcpy(buf, buff, len);
        return buf;
    }
    return NULL;
}

//----------------------------------
static void i2c_check_error(int err)
{
    if (err != ESP_OK) {
        char errs[32];
        sprintf(errs, "I2C bus error (%d)", err);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, errs));
    }
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
			mp_printf(print, "I2C (Port=%u, Mode=SLAVE, Speed=%u Hz, sda=%d, scl=%d, addr=%d, buffer=%d B, read-only=%d B)",
					self->bus_id, self->speed, self->sda, self->scl, self->slave_addr, self->slave_buflen, self->slave_rolen);
			mp_printf(print, "\n     Callback=%s (%d)", self->slave_cb ? "True" : "False", self->slave_cbtype);
		    if (i2c_slave_task_handle) {
		    	mp_printf(print, "\n     I2C task minimum free stack: %u", uxTaskGetStackHighWaterMark(i2c_slave_task_handle));
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
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "MASTER or SLAVE mode must be selected"));
    }

    if ((args[ARG_sda].u_obj == MP_OBJ_NULL) || (args[ARG_sda].u_obj == MP_OBJ_NULL)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "sda & scl must be given"));
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
	self->slave_rolen = 0;
	self->slave_addr = 0;
	self->slave_cb = NULL;
	self->slave_busy = false;
	self->slave_cbtype = I2C_SLAVE_CBTYPE_NONE;

	if (mode == I2C_MODE_MASTER) {
		// Setup I2C master
		if (i2c_init_master(self) != ESP_OK) {
	        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
		}
    }
    else {
        if (args[ARG_busy].u_int == 1) self->slave_busy = true;

        if (slave_mutex[self->bus_id] == NULL) {
    		slave_mutex[self->bus_id] = xSemaphoreCreateMutex();
    	}
		// Set I2C slave address
    	if (args[ARG_slaveaddr].u_int > 0) {
    		_checkAddr(args[ARG_slaveaddr].u_int);
        	self->slave_addr = args[ARG_slaveaddr].u_int;
    	}
    	else self->slave_addr = I2C_SLAVE_ADDR_DEFAULT;

    	// Set I2C slave buffers
    	if ((args[ARG_slavebuflen].u_int >= I2C_SLAVE_MIN_BUFFER_LENGTH) && (args[ARG_slavebuflen].u_int <= I2C_SLAVE_MAX_BUFFER_LENGTH))
    		self->slave_buflen = args[ARG_slavebuflen].u_int;
    	else self->slave_buflen = I2C_SLAVE_DEFAULT_BUFF_LEN;

    	if ((args[ARG_rolen].u_int > 0) && (args[ARG_rolen].u_int < (self->slave_buflen / 2)))
            self->slave_rolen = args[ARG_rolen].u_int;
        else self->slave_rolen = 0;
    	if ((self->slave_busy) && (self->slave_rolen == 0)) self->slave_rolen = 1;

    	if (i2c_init_slave(self, self->slave_busy) != ESP_OK) {
	        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
    	}

    	if (i2c_slave_task_handle == NULL) {
			#if CONFIG_MICROPY_USE_BOTH_CORES
    		int res = xTaskCreate(i2c_slave_task, "i2c_slave_task", I2C_SLAVE_TASK_STACK_SIZE, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle);
			#else
    		int res = xTaskCreatePinnedToCore(i2c_slave_task, "i2c_slave_task", I2C_SLAVE_TASK_STACK_SIZE, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle, MainTaskCore);
			#endif
    		if (res != pdPASS) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
    		}
    		vTaskDelay(100 / portTICK_PERIOD_MS);
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

    int8_t sda, scl, slave_addr;
    int32_t speed;
    uint8_t changed = 0;
    int buff_len, ro_len;

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
    buff_len = self->slave_buflen;
    ro_len = self->slave_rolen;
    slave_addr = self->slave_addr;

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

    if (mode == I2C_MODE_SLAVE) {
        if (args[ARG_busy].u_int >= 0) {
            if (self->slave_busy != ((args[ARG_busy].u_int == 1) ? true : false)) {
                self->slave_busy = (args[ARG_busy].u_int == 1) ? true : false;
                if ((self->slave_busy) && (self->slave_rolen == 0)) self->slave_rolen = 1;
                changed++;
            }
        }
        if (args[ARG_slaveaddr].u_int > 0) {
            _checkAddr(args[ARG_slaveaddr].u_int);
            if (args[ARG_slaveaddr].u_int != slave_addr) {
                slave_addr = args[ARG_slaveaddr].u_int;
                changed++;
            }
        }
        if ((args[ARG_slavebuflen].u_int >= I2C_SLAVE_MIN_BUFFER_LENGTH) && (args[ARG_slavebuflen].u_int <= I2C_SLAVE_MAX_BUFFER_LENGTH)) {
            if (args[ARG_slavebuflen].u_int != buff_len) {
                buff_len = args[ARG_slavebuflen].u_int;
                changed++;
            }
        }
        if ((args[ARG_rolen].u_int > 0) && (args[ARG_rolen].u_int < (buff_len/2))) {
            if (args[ARG_rolen].u_int != ro_len) {
                ro_len = args[ARG_rolen].u_int;
                if ((self->slave_busy) && (self->slave_rolen == 0)) self->slave_rolen = 1;
                changed++;
            }
        }
    }

    if (changed) {
    	if (i2c_used[old_bus_id] >= 0) {
    	    // Delete old driver, if it was a slave, the task will be stopped
            i2c_used[old_bus_id] = -1;
    		i2c_driver_delete(old_bus_id);
    		vTaskDelay(100 / portTICK_PERIOD_MS);
    	}
    	if (self->mode == I2C_MODE_SLAVE) {
            if (slave_mutex[self->bus_id] == NULL) {
                slave_mutex[self->bus_id] = xSemaphoreCreateMutex();
            }
            if (slave_mutex[self->bus_id]) xSemaphoreTake(slave_mutex[self->bus_id], I2C_SLAVE_MUTEX_TIMEOUT);
    	}
        if (i2c_used[bus_id] >= 0) {
        	if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex[self->bus_id])) xSemaphoreGive(slave_mutex[self->bus_id]);
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
	    	self->slave_buflen = buff_len;
            self->slave_rolen = ro_len;
	    	if (i2c_init_slave(self, self->slave_busy) != ESP_OK) {
	        	if (slave_mutex[self->bus_id]) xSemaphoreGive(slave_mutex[self->bus_id]);
		        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error installing I2C driver"));
	    	}
	    	if (i2c_slave_task_handle == NULL) {
				#if CONFIG_MICROPY_USE_BOTH_CORES
				int res = xTaskCreate(i2c_slave_task, "i2c_slave_task", I2C_SLAVE_TASK_STACK_SIZE, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle);
				#else
				int res = xTaskCreatePinnedToCore(i2c_slave_task, "i2c_slave_task", I2C_SLAVE_TASK_STACK_SIZE, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, &i2c_slave_task_handle, MainTaskCore);
				#endif
	            if (res != pdPASS) {
                    ESP_LOGE("[I2C]", "Error creating slave task");
	            }
	            vTaskDelay(100 / portTICK_PERIOD_MS);
	    	}
	    	else {
	    	    if (i2c_slave_add_task(self->bus_id, &i2c_slave_task_handle, &slave_state) != ESP_OK) {
	    	        ESP_LOGW("[I2C]", "Error adding slave task");
	    	    }
	    	}
	    }
		i2c_used[bus_id] = mode;
    	if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex[self->bus_id])) xSemaphoreGive(slave_mutex[self->bus_id]);
        self->mode = mode;
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

//-------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_is_ready(mp_obj_t self_in, mp_obj_t addr_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkMaster(self);

    int addr = mp_obj_get_int(addr_in);
    _checkAddr(addr);

    return hw_i2c_slave_detect(self, addr) ? mp_const_true : mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_i2c_is_ready_obj, mp_machine_i2c_is_ready);

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

    if (args[1].u_int > 0) {
        uint8_t *buf = heap_caps_malloc(args[1].u_int, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (buf == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating I2C data buffer"));
        }
        int ret = mp_i2c_master_read(self, args[0].u_int, false, 0, buf, args[1].u_int, false);
        if (ret != ESP_OK) {
            free(buf);
            i2c_check_error(ret);
        }
        vstr_t vstr;
        vstr_init_len(&vstr, args[1].u_int);
        memcpy(vstr.buf, buf, args[1].u_int);
        free(buf);
        // Return read data as string
        return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
    }
    return mp_const_empty_bytes;
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

    uint8_t *buf = get_buffer(bufinfo.buf, bufinfo.len);
    if (buf) {
        int ret = mp_i2c_master_read(self, args[0].u_int, false, 0, buf, bufinfo.len, false);
        if (ret != ESP_OK) {
            free(buf);
            i2c_check_error(ret);
        }
        memcpy(bufinfo.buf, buf, bufinfo.len);
        free(buf);
    }
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

    uint8_t *buf = get_buffer(bufinfo.buf, bufinfo.len);
    if (buf) {
        int ret = mp_i2c_master_write(self, args[0].u_int, 0, 0, buf, bufinfo.len, args[2].u_bool);
        free(buf);
        i2c_check_error(ret);
    }

    return mp_obj_new_int(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_writeto_obj, 1, mp_machine_i2c_writeto);

// Arguments for memory read/write methods
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

        uint8_t *buf = heap_caps_malloc(n, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (buf == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating I2C data buffer"));
        }
        int ret = mp_i2c_master_read(self, args[ARG_addr].u_int, memlen, addr, buf, n, args[ARG_stop].u_bool);
        if (ret != ESP_OK) {
            free(buf);
            i2c_check_error(ret);
        }
        vstr_t vstr;
        vstr_init_len(&vstr, n);
        memcpy(vstr.buf, buf, n);
        free(buf);
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

        uint8_t *buf = get_buffer(bufinfo.buf, bufinfo.len);
        if (buf) {
            // Transfer data into buffer
            int ret = mp_i2c_master_read(self, args[ARG_addr].u_int, memlen, addr, buf, bufinfo.len, args[ARG_stop].u_bool);
            if (ret != ESP_OK) {
                free(buf);
                i2c_check_error(ret);
            }
            memcpy(bufinfo.buf, buf, bufinfo.len);
            free(buf);
        }
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

    uint32_t addr = (uint32_t)mp_obj_get_int64(args[ARG_memaddr].u_obj);
    uint8_t memlen = getMemAdrLen(args[ARG_memlen].u_int, addr);

    uint8_t *buf = NULL;
    int len = 0;
    if (args[ARG_buf].u_obj != mp_const_none) {
        // Get the input data buffer
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[ARG_buf].u_obj, &bufinfo, MP_BUFFER_READ);
        buf = get_buffer(bufinfo.buf, bufinfo.len);
        len = bufinfo.len;
    }

    // Transfer address and data
    int ret = mp_i2c_master_write(self, args[ARG_addr].u_int, memlen, addr, buf, len, true);
    if (buf) free(buf);
    i2c_check_error(ret);
    return mp_obj_new_int(len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mp_machine_i2c_writeto_mem_obj, 1, mp_machine_i2c_writeto_mem);

//-------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_deinit(mp_obj_t self_in) {
    mp_machine_i2c_obj_t *self = self_in;

    if (i2c_used[self->bus_id] >= 0) {
    	if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex[self->bus_id])) xSemaphoreTake(slave_mutex[self->bus_id], I2C_SLAVE_MUTEX_TIMEOUT);

        i2c_used[self->bus_id] = -1;
    	i2c_driver_delete(self->bus_id);

        if ((self->mode == I2C_MODE_SLAVE) && (slave_mutex[self->bus_id])) xSemaphoreGive(slave_mutex[self->bus_id]);
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
		//self->rx_data = malloc(rxbuflen);
		self->rx_data = heap_caps_malloc(rxbuflen, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
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

//-----------------------------------------------------------------
void _check_addr_len(mp_machine_i2c_obj_t *self, int addr, int len)
{
    if ((len < 1) || (len > self->slave_buflen)) {
        mp_raise_ValueError("Length out of range");
    }
    if (addr >= self->slave_buflen) {
        mp_raise_ValueError("Address not in slave data buffer");
    }
    if ((addr + len) > self->slave_buflen) {
        mp_raise_ValueError("Data outside buffer");
    }
}

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_setdata(mp_obj_t self_in, mp_obj_t buf_in, mp_obj_t addr_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    int addr = mp_obj_get_int(addr_in);
    _check_addr_len(self, addr, bufinfo.len);

	if (slave_mutex[self->bus_id]) xSemaphoreTake(slave_mutex[self->bus_id], I2C_SLAVE_MUTEX_TIMEOUT);

    int res = i2c_slave_write_buffer(self->bus_id, bufinfo.buf, addr, bufinfo.len, I2C_SLAVE_MUTEX_TIMEOUT);

	if (slave_mutex[self->bus_id]) xSemaphoreGive(slave_mutex[self->bus_id]);

	if (res <= 0) return mp_const_false;
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_i2c_slave_setdata_obj, mp_machine_i2c_slave_setdata);

//---------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_reset_busy(mp_obj_t self_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    if (slave_mutex[self->bus_id]) xSemaphoreTake(slave_mutex[self->bus_id], I2C_SLAVE_MUTEX_TIMEOUT);

    int res = i2c_slave_reset_busy(self->bus_id, I2C_SLAVE_MUTEX_TIMEOUT);

    if (slave_mutex[self->bus_id]) xSemaphoreGive(slave_mutex[self->bus_id]);

    if (res <= 0) return mp_const_false;
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_machine_i2c_slave_reset_busy_obj, mp_machine_i2c_slave_reset_busy);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_getdata(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t len_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    int addr = mp_obj_get_int(addr_in);
    int len = mp_obj_get_int(len_in);
    _check_addr_len(self, addr, len);

    uint8_t *databuf = malloc(len);
    if (databuf == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating data buffer"));
    }

    mp_obj_t data;
    if (slave_mutex[self->bus_id]) xSemaphoreTake(slave_mutex[self->bus_id], I2C_SLAVE_MUTEX_TIMEOUT);

    int res = i2c_slave_read_buffer(self->bus_id, databuf, addr, len, I2C_SLAVE_MUTEX_TIMEOUT);
    if (res > 0) data = mp_obj_new_bytes(databuf, res);
    else data = mp_const_empty_bytes;

    if (slave_mutex[self->bus_id]) xSemaphoreGive(slave_mutex[self->bus_id]);

    free(databuf);
    // Return read data as byte array
    return data;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_i2c_slave_getdata_obj, mp_machine_i2c_slave_getdata);

//----------------------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_slave_callback(mp_obj_t self_in, mp_obj_t func, mp_obj_t type_in)
{
    mp_machine_i2c_obj_t *self = self_in;
    _checkSlave(self);

    int type = mp_obj_get_int(type_in);

    if ((!MP_OBJ_IS_FUN(func)) && (!MP_OBJ_IS_METH(func)) && (func != mp_const_none)) {
    	nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Function argument required"));
    }
    if ((type < 0) || (type > 7)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Invalid callback type"));
    }

	if (slave_mutex[self->bus_id]) xSemaphoreTake(slave_mutex[self->bus_id], I2C_SLAVE_MUTEX_TIMEOUT);

	if (func == mp_const_none) self->slave_cb = NULL;
	else self->slave_cb = func;
	self->slave_cbtype = type;

	if (slave_mutex[self->bus_id]) xSemaphoreGive(slave_mutex[self->bus_id]);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_i2c_slave_callback_obj, mp_machine_i2c_slave_callback);


//----------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_timing(size_t n_args, const mp_obj_t *args, int type)
{
    mp_machine_i2c_obj_t *self = args[0];

    int setup_time = -1;
    int hold_time = -1;
    if (n_args == 3) {
        setup_time = mp_obj_get_int(args[1]);
        hold_time = mp_obj_get_int(args[2]);
        if (type == 1) i2c_set_start_timing(self->bus_id, setup_time, hold_time);
        else if (type == 2) i2c_set_stop_timing(self->bus_id, setup_time, hold_time);
        else if (type == 3) i2c_set_data_timing(self->bus_id, setup_time, hold_time);
        else if (type == 4) i2c_set_period(self->bus_id, setup_time, hold_time);
    }
    if (type == 1) i2c_get_start_timing(self->bus_id, &setup_time, &hold_time);
    else if (type == 2) i2c_get_stop_timing(self->bus_id, &setup_time, &hold_time);
    else if (type == 3) i2c_get_data_timing(self->bus_id, &setup_time, &hold_time);
    else if (type == 4) i2c_get_period(self->bus_id, &setup_time, &hold_time);

    mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int(setup_time);
    tuple[1] = mp_obj_new_int(hold_time);
    return mp_obj_new_tuple(2, tuple);
}

//------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_start_timing(size_t n_args, const mp_obj_t *args)
{
    mp_obj_t res = mp_machine_i2c_timing(n_args, args, 1);
    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_i2c_start_timing_obj, 1, 3, mp_machine_i2c_start_timing);

//-----------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_stop_timing(size_t n_args, const mp_obj_t *args)
{
    mp_obj_t res = mp_machine_i2c_timing(n_args, args, 2);
    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_i2c_stop_timing_obj, 1, 3, mp_machine_i2c_stop_timing);

//-----------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_data_timing(size_t n_args, const mp_obj_t *args)
{
    mp_obj_t res = mp_machine_i2c_timing(n_args, args, 3);
    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_i2c_data_timing_obj, 1, 3, mp_machine_i2c_data_timing);

//------------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_clock_timing(size_t n_args, const mp_obj_t *args)
{
    mp_obj_t res = mp_machine_i2c_timing(n_args, args, 4);
    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_i2c_clock_timing_obj, 1, 3, mp_machine_i2c_clock_timing);

//-------------------------------------------------------------------------
STATIC mp_obj_t mp_machine_i2c_timeout(size_t n_args, const mp_obj_t *args)
{
    mp_machine_i2c_obj_t *self = args[0];
    _checkMaster(self);

    int tmo = -1;
    if (n_args == 2) {
        tmo = mp_obj_get_int(args[1]);
        i2c_set_timeout(self->bus_id, tmo * 80);
    }
    i2c_get_timeout(self->bus_id, &tmo);
    tmo /= 80;

    return mp_obj_new_int(tmo);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_i2c_timeout_obj, 1, 2, mp_machine_i2c_timeout);

//===================================================================
STATIC const mp_rom_map_elem_t mp_machine_i2c_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),                (mp_obj_t)&mp_machine_i2c_init_obj },
    { MP_ROM_QSTR(MP_QSTR_deinit),              (mp_obj_t)&mp_machine_i2c_deinit_obj },
    { MP_ROM_QSTR(MP_QSTR_scan),                (mp_obj_t)&mp_machine_i2c_scan_obj },
    { MP_ROM_QSTR(MP_QSTR_is_ready),            (mp_obj_t)&mp_machine_i2c_is_ready_obj },
    { MP_ROM_QSTR(MP_QSTR_start_timing),        (mp_obj_t)&mp_machine_i2c_start_timing_obj },
    { MP_ROM_QSTR(MP_QSTR_stop_timing),         (mp_obj_t)&mp_machine_i2c_stop_timing_obj },
    { MP_ROM_QSTR(MP_QSTR_data_timing),         (mp_obj_t)&mp_machine_i2c_data_timing_obj },
    { MP_ROM_QSTR(MP_QSTR_clock_timing),        (mp_obj_t)&mp_machine_i2c_clock_timing_obj },
    { MP_ROM_QSTR(MP_QSTR_timeout),             (mp_obj_t)&mp_machine_i2c_timeout_obj },

    // Standard methods
    { MP_ROM_QSTR(MP_QSTR_readfrom),            (mp_obj_t)&mp_machine_i2c_readfrom_obj },
    { MP_ROM_QSTR(MP_QSTR_readfrom_into),       (mp_obj_t)&mp_machine_i2c_readfrom_into_obj },
    { MP_ROM_QSTR(MP_QSTR_writeto),             (mp_obj_t)&mp_machine_i2c_writeto_obj },
    { MP_ROM_QSTR(MP_QSTR_setdata),       		(mp_obj_t)&mp_machine_i2c_slave_setdata_obj },
    { MP_ROM_QSTR(MP_QSTR_getdata),       		(mp_obj_t)&mp_machine_i2c_slave_getdata_obj },
    { MP_ROM_QSTR(MP_QSTR_resetbusy),           (mp_obj_t)&mp_machine_i2c_slave_reset_busy_obj },
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
    { MP_OBJ_NEW_QSTR(MP_QSTR_CBTYPE_NONE),     MP_OBJ_NEW_SMALL_INT(I2C_SLAVE_CBTYPE_NONE) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_CBTYPE_ADDR),     MP_OBJ_NEW_SMALL_INT(I2C_SLAVE_CBTYPE_ADDR) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_CBTYPE_RXDATA),   MP_OBJ_NEW_SMALL_INT(I2C_SLAVE_CBTYPE_DATA_RX) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_CBTYPE_TXDATA),   MP_OBJ_NEW_SMALL_INT(I2C_SLAVE_CBTYPE_DATA_TX) },
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
