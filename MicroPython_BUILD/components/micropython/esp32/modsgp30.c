/*
 * This implementation of the module uses the vendor's source code, located at:
 * https://github.com/Sensirion/embedded-sgp
 * (local path: components/micropython/esp32/libs/sgp)
 *
 * The module was created in accordance with the vendor's recommendations:
 * https://www.sensirion.com/fileadmin/user_upload/customers/sensirion/Dokumente/9_Gas_Sensors/Sensirion_Gas_Sensors_SGP30_Driver-Integration-Guide_HW_I2C.pdf
 *
 * Product page on vendor's site:
 * https://www.sensirion.com/en/environmental-sensors/gas-sensors/multi-pixel-gas-sensors/
 *
 * The SGP30 uses a dynamic baseline compensation algorithm and on-chip 
 * calibration parameters to provide two complementary air quality signals. 
 * Based on the sensor signals a total VOC signal (TVOC) and a CO2 equivalent 
 * signal (CO2eq) are calculated. After the “Init” command, a “Measure” commands
 * has to be sent in regular intervals of 1s to ensure proper operation of the 
 * dynamic baseline compensation algorithm. 
 * NOTE: For the first 15s after the “Init_air_quality” command the sensor is in 
 * an initialization phase during which a “Measure_air_quality” command returns 
 * fixed values of 400 ppm CO2eq and 0 ppb TVOC.
 *
 * See an example of use: modules_examples/sgp30_example.py
 *
 * Copyright (c) 2018 Alex Vrubel
 *
 *
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

#include "py/runtime.h"
#include "machine_hw_i2c.h"

#include "sensirion_i2c.h"
#include "sensirion_arch_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "machine_hw_i2c.h"
#include "sgp30.h"
#include <string.h>


const char * SGP_DRV_VERSION_STR = "3.1.1-dirty";
mp_machine_i2c_obj_t *i2c_obj;

typedef struct _sgp30_obj_t {
    mp_obj_base_t base;
} sgp30_obj_t;

const mp_obj_type_t sgp30_type;


// constructor(id, ...)
//-----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t sgp30_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	enum { ARG_i2c };
	static const mp_arg_t allowed_args[] = {
		{ MP_QSTR_i2c,	MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none}}
	};
	// parse arguments
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

	sgp30_obj_t *self = m_new_obj(sgp30_obj_t);
	self->base.type = &sgp30_type;
	i2c_obj = (mp_machine_i2c_obj_t *)MP_OBJ_TO_PTR(args[ARG_i2c].u_obj);

	return MP_OBJ_FROM_PTR(self);
}


//-----------------------------------------------------------------------------------------------
STATIC void sgp30_printinfo(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
	u16 tvoc_ppb;
	u16 co2_eq_ppm;
	sgp_read_iaq(&tvoc_ppb, &co2_eq_ppm);

	u16 ethanol_signal;
	u16 h2_signal;
	sgp_read_signals(&ethanol_signal, &h2_signal);

	mp_printf(print, "SGP30   (tvoc=%u ppb, co2_eq=%u ppm, ethanol=%u, h2=%u)\n", tvoc_ppb, co2_eq_ppm, ethanol_signal, h2_signal);
}



//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t sgp30_init(mp_obj_t self_in)
{
    return mp_obj_new_int(sgp_probe());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sgp30_init_obj, sgp30_init);



STATIC mp_obj_t get_serial_id(mp_obj_t self_in)
{
	u64 serial_id;
	sgp_get_serial_id(&serial_id);

    return mp_obj_new_int_from_ull(serial_id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(get_serial_id_obj, get_serial_id);



STATIC mp_obj_t get_driver_version(mp_obj_t self_in)
{
	const char * strver = sgp_get_driver_version();
    return mp_obj_new_str(strver, strlen(strver));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(get_driver_version_obj, get_driver_version);



STATIC mp_obj_t measure_co2_eq_blocking_read(mp_obj_t self_in)
{
	u16 co2_eq_ppm;
	sgp_measure_co2_eq_blocking_read(&co2_eq_ppm);

    return mp_obj_new_int(co2_eq_ppm);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_co2_eq_blocking_read_obj, measure_co2_eq_blocking_read);



STATIC mp_obj_t measure_tvoc_blocking_read(mp_obj_t self_in)
{
	u16 tvoc_ppb;
	sgp_measure_tvoc_blocking_read(&tvoc_ppb);
    return mp_obj_new_int(tvoc_ppb);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_tvoc_blocking_read_obj, measure_tvoc_blocking_read);



/*
 * The SGP30 also provides the possibility to read and write the baseline values 
 * of the baseline correction algorithm. This feature is used to save the 
 * baseline in regular intervals on an external non-volatile memory and restore 
 * it after a new power-up or soft reset of the sensor. 
 * The command “get_iaq_baseline” returns the baseline value for the two air 
 * quality signals. 
 * These value should be stored on an external memory. After a power-up or 
 * soft reset, the baseline of the baseline correction algorithm can be restored 
 * by call first an “sgp30_init” followed by a “set_iaq_baseline” with value as 
 * parameter.
 */
STATIC mp_obj_t get_iaq_baseline(mp_obj_t self_in)
{
	u32 baseline;
	sgp_get_iaq_baseline(&baseline);
    return mp_obj_new_int_from_uint(baseline);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(get_iaq_baseline_obj, get_iaq_baseline);



/*
 * Set baseline value for baseline correction algorithm.
 * See description for get_iaq_baseline method.
 */
STATIC mp_obj_t set_iaq_baseline(mp_obj_t self_in, mp_obj_t baseline)
{
	u32 bl = (u32)mp_obj_get_int64(baseline);
    return mp_obj_new_int(sgp_set_iaq_baseline(bl));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(set_iaq_baseline_obj, set_iaq_baseline);



STATIC mp_obj_t measure_iaq_blocking_read(mp_obj_t self_in)
{
	u16 tvoc_ppb;
	u16 co2_eq_ppm;
	sgp_measure_iaq_blocking_read(&tvoc_ppb, &co2_eq_ppm);

    mp_obj_t tuple[2];
	tuple[0] = mp_obj_new_int(co2_eq_ppm);
	tuple[1] = mp_obj_new_int(tvoc_ppb);
	return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_iaq_blocking_read_obj, measure_iaq_blocking_read);


/*
 * The commands “measure_signals...” is intended for part verification and 
 * testing purposes. It returns the sensor raw signals which are used as inputs
 * for the on-chip calibration and baseline compensation algorithms. 
 * The command performs a measurement to which the sensor responds with raw 
 * signals in the order H2_signal (sout_H2) and Ethanol_signal (sout_EthOH).
 */
STATIC mp_obj_t measure_signals_blocking_read(mp_obj_t self_in)
{
	u16 ethanol_signal;
	u16 h2_signal;
	sgp_measure_signals_blocking_read(&ethanol_signal, &h2_signal);

    mp_obj_t tuple[2];
	tuple[0] = mp_obj_new_int(ethanol_signal);
	tuple[1] = mp_obj_new_int(h2_signal);
	return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_signals_blocking_read_obj, measure_signals_blocking_read);



/*
 * The SGP30 features an on-chip humidity compensation for the air quality 
 * signals (CO 2eq and TVOC) and sensor raw signals (H2 and Ethanol). 
 * To use the on-chip humidity compensation an absolute humidity value from an 
 * external humidity sensor like the SHTxx is required. Using the 
 * “set_absolute_humidity” command, a new humidity value can be written to the 
 * SGP30. The 2 data bytes represent humidity values as a fixed-point 8.8bit 
 * number with a minimum value of 0x0001 (=1/256 g/m3) and a maximum value of 
 * 0xFFFF (255 g/m3 + 255/256 g/m3). For instance, sending a value of 0x0F80 
 * corresponds to a humidity value of 15.50 g/m3 (15 g/m3 + 128/256 g/m3). 
 * After setting a new humidity value, this value will be used by the on-chip 
 * humidity compensation algorithm until a new humidity value is set using the 
 * “set_absolute_humidity” command. Restarting the sensor (power-on or soft 
 * reset) or sending a value of 0x0000 (= 0 g/m3) sets the humidity value used 
 * for compensation to its default value (0x0B92 = 11.57 g/m3) until a new
 * humidity value is sent. Sending a humidity value of 0x0000 can therefore be 
 * used to turn off the humidity compensation.
 */
STATIC mp_obj_t set_absolute_humidity(mp_obj_t self_in, mp_obj_t absolute_humidity)
{
	u32 ah = (u32)mp_obj_get_int64(absolute_humidity);
    return mp_obj_new_int(sgp_set_absolute_humidity(ah));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(set_absolute_humidity_obj, set_absolute_humidity);



STATIC mp_obj_t get_feature_set_version(mp_obj_t self_in)
{
	u16 feature_set_version;
	u8 product_type;
	sgp_get_feature_set_version(&feature_set_version, &product_type);

    mp_obj_t tuple[2];
	tuple[0] = mp_obj_new_int(feature_set_version);
	tuple[1] = mp_obj_new_int(product_type);
	return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(get_feature_set_version_obj, get_feature_set_version);



STATIC mp_obj_t get_tvoc_factory_baseline(mp_obj_t self_in)
{
	u16 baseline;
	sgp_get_tvoc_factory_baseline(&baseline);
    return mp_obj_new_int_from_uint(baseline);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(get_tvoc_factory_baseline_obj, get_tvoc_factory_baseline);



STATIC mp_obj_t set_tvoc_baseline(mp_obj_t self_in, mp_obj_t baseline)
{
	u16 bl = (u16)mp_obj_get_int(baseline);
    return mp_obj_new_int(sgp_set_tvoc_baseline(bl));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(set_tvoc_baseline_obj, set_tvoc_baseline);



STATIC mp_obj_t get_configured_address(mp_obj_t self_in)
{
    return mp_obj_new_int_from_uint(sgp_get_configured_address());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(get_configured_address_obj, get_configured_address);



/*
 * The command “measure_test” which is included for integration and 
 * production line testing runs an on-chip self-test. In case of a successful 
 * self-test the sensor returns the fixed data pattern 0xD400 (with correct CRC).
 */
STATIC mp_obj_t measure_test(mp_obj_t self_in)
{
	u16 test_result;
	sgp_measure_test(&test_result);
    return mp_obj_new_int_from_uint(test_result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_test_obj, measure_test);



STATIC mp_obj_t measure_tvoc(mp_obj_t self_in)
{
    return mp_obj_new_int(sgp_measure_tvoc());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_tvoc_obj, measure_tvoc);



STATIC mp_obj_t measure_iaq(mp_obj_t self_in)
{
    return mp_obj_new_int(sgp_measure_iaq());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_iaq_obj, measure_iaq);



STATIC mp_obj_t measure_co2_eq(mp_obj_t self_in)
{
    return mp_obj_new_int(sgp_measure_co2_eq());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_co2_eq_obj, measure_co2_eq);



STATIC mp_obj_t measure_signals(mp_obj_t self_in)
{
    return mp_obj_new_int(sgp_measure_signals());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(measure_signals_obj, measure_signals);



STATIC mp_obj_t read_co2_eq(mp_obj_t self_in)
{
	u16 result;
	sgp_read_co2_eq(&result);
    return mp_obj_new_int_from_uint(result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(read_co2_eq_obj, read_co2_eq);



STATIC mp_obj_t read_tvoc(mp_obj_t self_in)
{
	u16 result;
	sgp_read_tvoc(&result);
    return mp_obj_new_int_from_uint(result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(read_tvoc_obj, read_tvoc);



STATIC mp_obj_t read_iaq(mp_obj_t self_in)
{
	u16 tvoc_ppb;
	u16 co2_eq_ppm;
	sgp_read_iaq(&tvoc_ppb, &co2_eq_ppm);

    mp_obj_t tuple[2];
	tuple[0] = mp_obj_new_int(co2_eq_ppm);
	tuple[1] = mp_obj_new_int(tvoc_ppb);
	return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(read_iaq_obj, read_iaq);



STATIC mp_obj_t read_signals(mp_obj_t self_in)
{
	u16 ethanol_signal;
	u16 h2_signal;
	sgp_read_signals(&ethanol_signal, &h2_signal);

    mp_obj_t tuple[2];
	tuple[0] = mp_obj_new_int(ethanol_signal);
	tuple[1] = mp_obj_new_int(h2_signal);
	return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(read_signals_obj, read_signals);




//===============================================================================
STATIC const mp_rom_map_elem_t sgp30_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init),								MP_ROM_PTR(&sgp30_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_serial_id),						MP_ROM_PTR(&get_serial_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_driver_version),					MP_ROM_PTR(&get_driver_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_co2_eq_blocking_read),		MP_ROM_PTR(&measure_co2_eq_blocking_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_tvoc_blocking_read),			MP_ROM_PTR(&measure_tvoc_blocking_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_iaq_baseline),					MP_ROM_PTR(&get_iaq_baseline_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_iaq_baseline),					MP_ROM_PTR(&set_iaq_baseline_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_iaq_blocking_read),			MP_ROM_PTR(&measure_iaq_blocking_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_signals_blocking_read),		MP_ROM_PTR(&measure_signals_blocking_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_absolute_humidity),				MP_ROM_PTR(&set_absolute_humidity_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_feature_set_version),				MP_ROM_PTR(&get_feature_set_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_tvoc_factory_baseline),			MP_ROM_PTR(&get_tvoc_factory_baseline_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_tvoc_baseline),					MP_ROM_PTR(&set_tvoc_baseline_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_configured_address),				MP_ROM_PTR(&get_configured_address_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_test),						MP_ROM_PTR(&measure_test_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_tvoc),						MP_ROM_PTR(&measure_tvoc_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_iaq),							MP_ROM_PTR(&measure_iaq_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_co2_eq),						MP_ROM_PTR(&measure_co2_eq_obj) },
    { MP_ROM_QSTR(MP_QSTR_measure_signals),						MP_ROM_PTR(&measure_signals_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_co2_eq),							MP_ROM_PTR(&read_co2_eq_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_tvoc),							MP_ROM_PTR(&read_tvoc_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_iaq),							MP_ROM_PTR(&read_iaq_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_signals),						MP_ROM_PTR(&read_signals_obj) }
};
STATIC MP_DEFINE_CONST_DICT(sgp30_locals_dict, sgp30_locals_dict_table);

//===============================================================================
const mp_obj_type_t sgp30_type = {
    { &mp_type_type },
    .name = MP_QSTR_SGP30,
    .print = sgp30_printinfo,
    .make_new = sgp30_make_new,
    .locals_dict = (mp_obj_t)&sgp30_locals_dict,
};



//===============================================================================
STATIC const mp_rom_map_elem_t sgp30_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sgp30) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_SGP30), MP_ROM_PTR(&sgp30_type) },
};

//===============================================================================
STATIC MP_DEFINE_CONST_DICT(sgp30_module_globals, sgp30_module_globals_table);

const mp_obj_module_t mp_module_sgp30 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&sgp30_module_globals,
};




//===============================================//
//                                               //
// Functions required for vendor's source code   //
//                                               //
//===============================================//


/**
 * Initialize all hard- and software components that are needed for the I2C
 * communication.
 */
void sensirion_i2c_init()
{
    // IMPLEMENT
}



/**
 * Execute one read transaction on the I2C bus, reading a given number of bytes.
 * If the device does not acknowledge the read command, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to read from
 * @param data    pointer to the buffer where the data is to be stored
 * @param count   number of bytes to read from I2C and store in the buffer
 * @returns 0 on success, error code otherwise
 */
s8 sensirion_i2c_read(u8 address, u8* data, u16 count)
{
	return mp_i2c_master_read(i2c_obj, address, 0, 0, data, count, true);
}



/**
 * Execute one write transaction on the I2C bus, sending a given number of bytes.
 * The bytes in the supplied buffer must be sent to the given address. If the
 * slave device does not acknowledge any of the bytes, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to write to
 * @param data    pointer to the buffer containing the data to write
 * @param count   number of bytes to read from the buffer and send over I2C
 * @returns 0 on success, error code otherwise
 */
s8 sensirion_i2c_write(u8 address, const u8* data, u16 count)
{
	return mp_i2c_master_write(i2c_obj, address, 0, 0, (uint8_t *)data, count, true);
}



/**
 * Sleep for a given number of microseconds. The function should delay the
 * execution for at least the given time, but may also sleep longer.
 *
 * Despite the unit, a <10 millisecond precision is sufficient.
 *
 * @param useconds the sleep time in microseconds
 */
void sensirion_sleep_usec(u32 useconds) 
{
	u32 msec = useconds / 1000;
	if (useconds % 1000 > 0) {
		msec++;
	}
	vTaskDelay(msec / portTICK_PERIOD_MS);
}
