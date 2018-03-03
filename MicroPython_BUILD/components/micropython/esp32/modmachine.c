/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2015 Damien P. George
 * Copyright (c) 2016 Paul Sokolovsky
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
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"
#include "esp_system.h"
#include "soc/dport_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "rom/uart.h"
#include "esp_deep_sleep.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "extmod/machine_mem.h"
#include "extmod/machine_signal.h"
#include "extmod/machine_pulse.h"
#include "extmod/vfs_native.h"
#include "modmachine.h"
#include "mpsleep.h"
#include "machine_rtc.h"
#include "uart.h"

#if MICROPY_PY_MACHINE

nvs_handle mpy_nvs_handle = 0;

extern machine_rtc_config_t machine_rtc_config;

//---------------------------------------------
void prepareSleepReset(uint8_t hrst, char *msg)
{
    // Umount external & internal fs
	externalUmount();
	internalUmount();

	if (!hrst) {
		mp_thread_deinit();

		if (msg) mp_hal_stdout_tx_str(msg);

		// deinitialise peripherals
		machine_pins_deinit();

		mp_deinit();
		fflush(stdout);
	}
}

//-----------------------------------------------------------------
STATIC mp_obj_t machine_freq(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        // get
        return mp_obj_new_int(ets_get_cpu_frequency() * 1000000);
    }
    else {
        // set
        mp_int_t freq = mp_obj_get_int(args[0]) / 1000000;
        if (freq != 80 && freq != 160 && freq != 240) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError,
                "frequency can only be either 80Mhz, 160MHz or 240MHz"));
        }
        /*
        system_update_cpu_freq(freq);
        */
        return mp_const_none;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_freq_obj, 0, 1, machine_freq);

//-----------------------------------
STATIC mp_obj_t machine_reset(void) {
    prepareSleepReset(1, NULL);

    esp_restart(); // This function does not return.

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_reset_obj, machine_reset);

//---------------------------------------
STATIC mp_obj_t machine_unique_id(void) {
    uint8_t chipid[6];
    esp_efuse_mac_get_default(chipid);
    return mp_obj_new_bytes(chipid, 6);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_unique_id_obj, machine_unique_id);

//----------------------------------
STATIC mp_obj_t machine_idle(void) {
    taskYIELD();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_idle_obj, machine_idle);

//-----------------------------------------
STATIC mp_obj_t machine_disable_irq(void) {
    uint32_t state = MICROPY_BEGIN_ATOMIC_SECTION();
    return mp_obj_new_int(state);
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_disable_irq_obj, machine_disable_irq);

//-----------------------------------------------------
STATIC mp_obj_t machine_enable_irq(mp_obj_t state_in) {
    uint32_t state = mp_obj_get_int(state_in);
    MICROPY_END_ATOMIC_SECTION(state);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_enable_irq_obj, machine_enable_irq);

//--------------------------------------------------
static void print_heap_info(multi_heap_info_t *info)
{
	mp_printf(&mp_plat_print, "              Free: %u\n", info->total_free_bytes);
	mp_printf(&mp_plat_print, "         Allocated: %u\n", info->total_allocated_bytes);
	mp_printf(&mp_plat_print, "      Minimum free: %u\n", info->minimum_free_bytes);
	mp_printf(&mp_plat_print, "      Total blocks: %u\n", info->total_blocks);
	mp_printf(&mp_plat_print, "Largest free block: %u\n", info->largest_free_block);
	mp_printf(&mp_plat_print, "  Allocated blocks: %u\n", info->allocated_blocks);
	mp_printf(&mp_plat_print, "       Free blocks: %u\n", info->free_blocks);
}

//---------------------------------------
STATIC mp_obj_t machine_heap_info(void) {
	multi_heap_info_t info;

    mp_printf(&mp_plat_print, "Heap outside of MicroPython heap:\n---------------------------------\n");

	heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
	print_heap_info(&info);

	#if CONFIG_SPIRAM_SUPPORT
		#if SPIRAM_USE_MEMMAP
			mp_printf(&mp_plat_print, "\nSPIRAM info (MEMMAP used):\n--------------------------\n");
			mp_printf(&mp_plat_print, "Total: %u\n", CONFIG_SPIRAM_SIZE);
			mp_printf(&mp_plat_print, " Free: %u\n", CONFIG_SPIRAM_SIZE - (CONFIG_MICROPY_HEAP_SIZE * 1024);
		#else
			mp_printf(&mp_plat_print, "\nSPIRAM info:\n------------\n");
			heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
			print_heap_info(&info);
		#endif
	#endif

	return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_heap_info_obj, machine_heap_info);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_deepsleep(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum {ARG_sleep_ms};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_sleep_ms, MP_ARG_INT, { .u_int = 0 } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);


    mp_int_t expiry = args[ARG_sleep_ms].u_int;

    if (expiry > 0) {
        esp_deep_sleep_enable_timer_wakeup((uint64_t)(expiry * 1000));
    }
    else {
        if ((machine_rtc_config.ext0_pin < 0) && (machine_rtc_config.ext1_pins == 0) && (!machine_rtc_config.wake_on_touch))  {
            mp_raise_ValueError("No other wake-up sources configured, sleep time cannot be 0 !");
        }
    }

    if (machine_rtc_config.ext0_pin != -1) {
        esp_deep_sleep_enable_ext0_wakeup(machine_rtc_config.ext0_pin, machine_rtc_config.ext0_level ? 1 : 0);
    }

    if (machine_rtc_config.ext1_pins != 0) {
    	//esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
        esp_deep_sleep_enable_ext1_wakeup(
            machine_rtc_config.ext1_pins,
            machine_rtc_config.ext1_level ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ALL_LOW);
    }

    if (machine_rtc_config.wake_on_touch) {
        esp_deep_sleep_enable_touchpad_wakeup();
    }

    prepareSleepReset(0, "ESP32: DEEP SLEEP\n");

    esp_deep_sleep_start(); // This function does not return.

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_deepsleep_obj, 0,  machine_deepsleep);

//------------------------------------------
STATIC mp_obj_t machine_wake_reason (void) {
    mpsleep_reset_cause_t reset_reason = mpsleep_get_reset_cause ();
    mpsleep_wake_reason_t wake_reason = mpsleep_get_wake_reason();
    mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int(reset_reason);
    tuple[1] = mp_obj_new_int(wake_reason);
    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_wake_reason_obj, machine_wake_reason);

//----------------------------------------
STATIC mp_obj_t machine_wake_desc (void) {
    char reason[24] = { 0 };
    mp_obj_t tuple[2];

    mpsleep_get_reset_desc(reason);
    tuple[0] = mp_obj_new_str(reason, strlen(reason), 0);
    mpsleep_get_wake_desc(reason);
    tuple[1] = mp_obj_new_str(reason, strlen(reason), 0);
    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_wake_desc_obj, machine_wake_desc);


//-----------------------------------------------------------------------
STATIC mp_obj_t machine_stdin_get (mp_obj_t sz_in, mp_obj_t timeout_in) {
    mp_int_t timeout = mp_obj_get_int(timeout_in);
    mp_int_t sz = mp_obj_get_int(sz_in);
    if (sz == 0) {
        return mp_const_none;
    }
    int c = -1;
    vstr_t vstr;
    mp_int_t recv = 0;

    vstr_init_len(&vstr, sz);

	xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
	uart0_raw_input = 1;
	xSemaphoreGive(uart0_mutex);

	while (recv < sz) {
    	c = mp_hal_stdin_rx_chr(timeout);
    	if (c < 0) break;
    	vstr.buf[recv++] = (byte)c;
    }

    xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
	uart0_raw_input = 0;
	xSemaphoreGive(uart0_mutex);

	if (recv == 0) {
        return mp_const_none;
	}
    return mp_obj_new_str_from_vstr(&mp_type_str, &vstr);;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_stdin_get_obj, machine_stdin_get);

//----------------------------------------------------
STATIC mp_obj_t machine_stdout_put (mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    mp_int_t len = bufinfo.len;
    char *buf = bufinfo.buf;

	xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
	uart0_raw_input = 1;
	xSemaphoreGive(uart0_mutex);

	mp_hal_stdout_tx_strn(buf, len);

	xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
	uart0_raw_input = 0;
	xSemaphoreGive(uart0_mutex);

    return mp_obj_new_int_from_uint(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_stdout_put_obj, machine_stdout_put);


// Assumes 0 <= max <= RAND_MAX
// Returns in the closed interval [0, max]
//--------------------------------------------
STATIC uint64_t random_at_most(uint32_t max) {
	uint64_t	// max <= RAND_MAX < ULONG_MAX, so this is okay.
	num_bins = (uint64_t) max + 1,
	num_rand = (uint64_t) 0xFFFFFFFF + 1,
	bin_size = num_rand / num_bins,
	defect   = num_rand % num_bins;

	uint32_t x;
	do {
		x = esp_random();
	}
	while (num_rand - defect <= (uint64_t)x); // This is carefully written not to overflow

	// Truncated division is intentional
	return x/bin_size;
}

//-----------------------------------------------------------------
STATIC mp_obj_t machine_random(size_t n_args, const mp_obj_t *args)
{
	if (n_args == 1) {
		uint32_t rmax = mp_obj_get_int(args[0]);
	    return mp_obj_new_int_from_uint(random_at_most(rmax));
	}
	uint32_t rmin = mp_obj_get_int(args[0]);
	uint32_t rmax = mp_obj_get_int(args[1]);
	return mp_obj_new_int_from_uint(rmin + random_at_most(rmax - rmin));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_random_obj, 1, 2, machine_random);


// ==== NVS Support ===================================================================

static void checkNVS()
{
    if (mpy_nvs_handle == 0) {
    	mp_raise_msg(&mp_type_OSError, "NVS not available!");
    }
}

//------------------------------------------------------------------------
STATIC mp_obj_t mod_machine_nvs_set_int (mp_obj_t _key, mp_obj_t _value) {
	checkNVS();

	const char *key = mp_obj_str_get_str(_key);
    uint32_t value = mp_obj_get_int_truncated(_value);

    esp_err_t esp_err = nvs_set_i32(mpy_nvs_handle, key, value);
    if (ESP_OK == esp_err) {
        nvs_commit(mpy_nvs_handle);
    }
    else if (ESP_ERR_NVS_NOT_ENOUGH_SPACE == esp_err || ESP_ERR_NVS_PAGE_FULL == esp_err || ESP_ERR_NVS_NO_FREE_PAGES == esp_err) {
    	mp_raise_msg(&mp_type_OSError, "No space available.");
    }
    else if (ESP_ERR_NVS_INVALID_NAME == esp_err || ESP_ERR_NVS_KEY_TOO_LONG == esp_err) {
    	mp_raise_msg(&mp_type_OSError, "Key invalid or too long");
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mod_machine_nvs_set_int_obj, mod_machine_nvs_set_int);

//-------------------------------------------------------
STATIC mp_obj_t mod_machine_nvs_get_int (mp_obj_t _key) {
	checkNVS();

    const char *key = mp_obj_str_get_str(_key);
    int value = 0;

    if (ESP_ERR_NVS_NOT_FOUND == nvs_get_i32(mpy_nvs_handle, key, &value)) {
        return mp_const_none;
    }
    return mp_obj_new_int(value);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_machine_nvs_get_int_obj, mod_machine_nvs_get_int);

//------------------------------------------------------------------------
STATIC mp_obj_t mod_machine_nvs_set_str (mp_obj_t _key, mp_obj_t _value) {
	checkNVS();

	const char *key = mp_obj_str_get_str(_key);
	const char *value = mp_obj_str_get_str(_value);

    esp_err_t esp_err = nvs_set_str(mpy_nvs_handle, key, value);
    if (ESP_OK == esp_err) {
        nvs_commit(mpy_nvs_handle);
    }
    else if (ESP_ERR_NVS_NOT_ENOUGH_SPACE == esp_err || ESP_ERR_NVS_PAGE_FULL == esp_err || ESP_ERR_NVS_NO_FREE_PAGES == esp_err) {
    	mp_raise_msg(&mp_type_OSError, "No space available.");
    }
    else if (ESP_ERR_NVS_INVALID_NAME == esp_err || ESP_ERR_NVS_KEY_TOO_LONG == esp_err) {
    	mp_raise_msg(&mp_type_OSError, "Key invalid or too long");
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mod_machine_nvs_set_str_obj, mod_machine_nvs_set_str);

//-------------------------------------------------------
STATIC mp_obj_t mod_machine_nvs_get_str (mp_obj_t _key) {
	checkNVS();

    const char *key = mp_obj_str_get_str(_key);
    size_t len = 0;
    mp_obj_t strval = mp_const_none;

    esp_err_t ret = nvs_get_str(mpy_nvs_handle, key, NULL, &len);
    if ((ret == ESP_OK ) && (len > 0)) {
        char *value = malloc(len);
        if (value) {
            esp_err_t ret = nvs_get_str(mpy_nvs_handle, key, value, &len);
            if ((ret == ESP_OK ) && (len > 0)) {
                strval = mp_obj_new_str(value, strlen(value), 0);
                free(value);
            }
        }
    }
    return strval;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_machine_nvs_get_str_obj, mod_machine_nvs_get_str);

//-----------------------------------------------------
STATIC mp_obj_t mod_machine_nvs_erase (mp_obj_t _key) {
	checkNVS();

    const char *key = mp_obj_str_get_str(_key);

    if (ESP_ERR_NVS_NOT_FOUND == nvs_erase_key(mpy_nvs_handle, key)) {
        mp_raise_ValueError("Key not found");
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_machine_nvs_erase_obj, mod_machine_nvs_erase);

//------------------------------------------------
STATIC mp_obj_t mod_machine_nvs_erase_all (void) {
	checkNVS();

    if (ESP_OK != nvs_erase_all(mpy_nvs_handle)) {
    	mp_raise_msg(&mp_type_OSError, "Operation failed.");
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_nvs_erase_all_obj, mod_machine_nvs_erase_all);


// ==== ESP32 log level ===================================================================

static vprintf_like_t orig_log_func = NULL;

//--------------------------------------------------------
static int vprintf_redirected(const char *fmt, va_list ap)
{
    int ret = mp_vprintf(&mp_plat_print, fmt, ap);
    return ret;
}

static vprintf_like_t mp_log_func = &vprintf_redirected;

//--------------------------------------------------------------------------
STATIC mp_obj_t mod_machine_log_level (mp_obj_t tag_in, mp_obj_t level_in) {
	const char *tag = mp_obj_str_get_str(tag_in);
    int32_t level = mp_obj_get_int(level_in);
    if ((level < 0) || (level > 5)) {
    	mp_raise_ValueError("Log level 0~5 expected");
    }

	esp_log_level_set(tag, level);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mod_machine_log_level_obj, mod_machine_log_level);

//---------------------------------------
STATIC mp_obj_t mod_machine_logto_mp () {
	if (orig_log_func == NULL) {
		orig_log_func = esp_log_set_vprintf(mp_log_func);
	}
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_logto_mp_obj, mod_machine_logto_mp);

//----------------------------------------
STATIC mp_obj_t mod_machine_logto_esp () {
	if (orig_log_func != NULL) {
		vprintf_like_t prev_func = esp_log_set_vprintf(orig_log_func);
		orig_log_func = NULL;
	}
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_logto_esp_obj, mod_machine_logto_esp);

extern uint8_t temprature_sens_read();

//-----------------------------------
STATIC mp_obj_t mod_machine_tsens() {
/*
	SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3, SENS_FORCE_XPD_SAR_S);
    SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10, SENS_TSENS_CLK_DIV_S);
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    ets_delay_us(100);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    ets_delay_us(5);
    //while(REG_GET_FIELD(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_RDY_OUT) == 0) {
    //    ;
    //}
    int res = GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT, SENS_TSENS_OUT_S);

    // (res - 32) / 1.8 --> temperature in C
    return MP_OBJ_NEW_SMALL_INT(res);
*/
	int temper = temprature_sens_read();
	float ftemper = (float)(temper -32) / 1.8;

	mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int_from_uint(temper);
    tuple[1] = mp_obj_new_float(ftemper);
    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_tsens_obj, mod_machine_tsens);

//--------------------------------------------------------------
STATIC mp_obj_t mod_machine_stdin_disable(mp_obj_t pattern_in) {
	const char *pattern = mp_obj_str_get_str(pattern_in);
	if (strlen(pattern) >= 16) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "pattern string too long (15 chars allowed)"));
	}
	disableStdin(pattern);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_stdin_disable_obj, mod_machine_stdin_disable);

//===============================================================
STATIC const mp_rom_map_elem_t machine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_umachine) },

    { MP_ROM_QSTR(MP_QSTR_mem8),					MP_ROM_PTR(&machine_mem8_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem16),					MP_ROM_PTR(&machine_mem16_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem32),					MP_ROM_PTR(&machine_mem32_obj) },

    { MP_ROM_QSTR(MP_QSTR_freq),					MP_ROM_PTR(&machine_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset),					MP_ROM_PTR(&machine_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_unique_id),				MP_ROM_PTR(&machine_unique_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_idle),					MP_ROM_PTR(&machine_idle_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_deepsleep),			MP_ROM_PTR(&machine_deepsleep_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_wake_reason),			MP_ROM_PTR(&machine_wake_reason_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_wake_description),	MP_ROM_PTR(&machine_wake_desc_obj) },
    { MP_ROM_QSTR(MP_QSTR_heap_info),				MP_ROM_PTR(&machine_heap_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_stdin_disable),			MP_ROM_PTR(&mod_machine_stdin_disable_obj) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_setint),			MP_ROM_PTR(&mod_machine_nvs_set_int_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_getint),			MP_ROM_PTR(&mod_machine_nvs_get_int_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_setstr),			MP_ROM_PTR(&mod_machine_nvs_set_str_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_getstr),			MP_ROM_PTR(&mod_machine_nvs_get_str_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_erase),			MP_ROM_PTR(&mod_machine_nvs_erase_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_erase_all),		MP_ROM_PTR(&mod_machine_nvs_erase_all_obj) },

    { MP_OBJ_NEW_QSTR(MP_QSTR_loglevel),			MP_ROM_PTR(&mod_machine_log_level_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_redirectlog),			MP_ROM_PTR(&mod_machine_logto_mp_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_restorelog),			MP_ROM_PTR(&mod_machine_logto_esp_obj) },
	{ MP_ROM_QSTR(MP_QSTR_LOG_NONE),				MP_ROM_INT(ESP_LOG_NONE) },
	{ MP_ROM_QSTR(MP_QSTR_LOG_ERROR),				MP_ROM_INT(ESP_LOG_ERROR) },
	{ MP_ROM_QSTR(MP_QSTR_LOG_WARN),				MP_ROM_INT(ESP_LOG_WARN) },
	{ MP_ROM_QSTR(MP_QSTR_LOG_INFO),				MP_ROM_INT(ESP_LOG_INFO) },
	{ MP_ROM_QSTR(MP_QSTR_LOG_DEBUG),				MP_ROM_INT(ESP_LOG_DEBUG) },
	{ MP_ROM_QSTR(MP_QSTR_LOG_VERBOSE),				MP_ROM_INT(ESP_LOG_VERBOSE) },

	{ MP_ROM_QSTR(MP_QSTR_stdin_get),				MP_ROM_PTR(&machine_stdin_get_obj) },
	{ MP_ROM_QSTR(MP_QSTR_stdout_put),				MP_ROM_PTR(&machine_stdout_put_obj) },

    { MP_ROM_QSTR(MP_QSTR_disable_irq),				MP_ROM_PTR(&machine_disable_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable_irq),				MP_ROM_PTR(&machine_enable_irq_obj) },

    { MP_ROM_QSTR(MP_QSTR_time_pulse_us),			MP_ROM_PTR(&machine_time_pulse_us_obj) },

    { MP_ROM_QSTR(MP_QSTR_random),					MP_ROM_PTR(&machine_random_obj) },
    { MP_ROM_QSTR(MP_QSTR_internal_temp),			MP_ROM_PTR(&mod_machine_tsens_obj) },

	{ MP_ROM_QSTR(MP_QSTR_Timer),					MP_ROM_PTR(&machine_timer_type) },
    { MP_ROM_QSTR(MP_QSTR_Pin),						MP_ROM_PTR(&machine_pin_type) },
    { MP_ROM_QSTR(MP_QSTR_Signal),					MP_ROM_PTR(&machine_signal_type) },
    { MP_ROM_QSTR(MP_QSTR_TouchPad),				MP_ROM_PTR(&machine_touchpad_type) },
    { MP_ROM_QSTR(MP_QSTR_ADC),						MP_ROM_PTR(&machine_adc_type) },
    { MP_ROM_QSTR(MP_QSTR_DAC),						MP_ROM_PTR(&machine_dac_type) },
    { MP_ROM_QSTR(MP_QSTR_I2C),						MP_ROM_PTR(&machine_hw_i2c_type) },
    { MP_ROM_QSTR(MP_QSTR_PWM),						MP_ROM_PTR(&machine_pwm_type) },
    { MP_ROM_QSTR(MP_QSTR_SPI),						MP_ROM_PTR(&machine_hw_spi_type) },
    { MP_ROM_QSTR(MP_QSTR_UART),					MP_ROM_PTR(&machine_uart_type) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_RTC),					MP_ROM_PTR(&mach_rtc_type) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Neopixel),			MP_ROM_PTR(&machine_neopixel_type) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_DHT),					MP_ROM_PTR(&machine_dht_type) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_Onewire),				MP_ROM_PTR(&machine_onewire_type) },
};
STATIC MP_DEFINE_CONST_DICT(machine_module_globals, machine_module_globals_table);

//=========================================
const mp_obj_module_t mp_module_machine = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&machine_module_globals,
};

#endif // MICROPY_PY_MACHINE
