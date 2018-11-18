/*
 * machine_hw_i2c.h
 *
 *  Created on: 31 июл. 2018 г.
 *      Author: vrubel
 */

#ifndef COMPONENTS_MICROPYTHON_ESP32_MACHINE_HW_I2C_H_
#define COMPONENTS_MICROPYTHON_ESP32_MACHINE_HW_I2C_H_

#include "driver/i2c.h"
#include "py/obj.h"

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

int mp_i2c_master_write(mp_machine_i2c_obj_t *i2c_obj, uint16_t slave_addr, uint8_t memwrite, uint32_t memaddr, uint8_t *data, uint16_t len, bool stop);
int mp_i2c_master_read(mp_machine_i2c_obj_t *i2c_obj, uint16_t slave_addr, uint8_t memread, uint32_t memaddr, uint8_t *data, uint16_t len, bool stop);

#endif /* COMPONENTS_MICROPYTHON_ESP32_MACHINE_HW_I2C_H_ */
