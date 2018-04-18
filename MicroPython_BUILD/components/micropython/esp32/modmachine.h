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

#ifndef MICROPY_INCLUDED_ESP32_MODMACHINE_H
#define MICROPY_INCLUDED_ESP32_MODMACHINE_H

#include "sdkconfig.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "py/obj.h"
#include "driver/rtc_io.h"

#define MPY_MIN_STACK_SIZE	(6*1024)
#if CONFIG_SPIRAM_SUPPORT
#define MPY_MAX_STACK_SIZE	(64*1024)
#define MPY_MIN_HEAP_SIZE	(128*1024)
#define MPY_MAX_HEAP_SIZE	(3584*1024)
#else
#define MPY_MAX_STACK_SIZE	(32*1024)
#define MPY_MIN_HEAP_SIZE	(48*1024)
#if defined(CONFIG_MICROPY_USE_CURL) && defined(CONFIG_MICROPY_USE_CURL_TLS)
#define MPY_MAX_HEAP_SIZE	(72*1024)
#else
#define MPY_MAX_HEAP_SIZE	(96*1024)
#endif
#endif

#define EXT1_WAKEUP_ALL_HIGH	2    //!< Wake the chip when all selected GPIOs go high
#define EXT1_WAKEUP_MAX_PINS	4

typedef struct {
    int8_t		ext1_pins[EXT1_WAKEUP_MAX_PINS];
    uint8_t		ext1_rtcpins[EXT1_WAKEUP_MAX_PINS];
    uint8_t		ext1_level;
    int8_t		ext0_pin;
    int8_t		ext0_rtcpin;
    uint8_t		ext0_level;
    size_t		ext0_count;
    uint64_t	ext0_last_time;
    uint8_t		wake_on_touch;
    size_t		pulse_count;
    uint32_t	deepsleep_time;
    uint32_t	deepsleep_interval;
    uint64_t	stub_wait;
    uint64_t	wakeup_delay_ticks;
    uint64_t	wakeup_delay_ticks_last;
    int8_t		stub_outpin;
    uint8_t		stub_outpin_level;
} machine_rtc_config_t;

extern machine_rtc_config_t machine_rtc_config;

extern const mp_obj_type_t machine_timer_type;
extern const mp_obj_type_t machine_pin_type;
extern const mp_obj_type_t machine_touchpad_type;
extern const mp_obj_type_t machine_adc_type;
extern const mp_obj_type_t machine_dac_type;
extern const mp_obj_type_t machine_pwm_type;
extern const mp_obj_type_t machine_hw_spi_type;
extern const mp_obj_type_t machine_hw_i2c_type;
extern const mp_obj_type_t machine_uart_type;
extern const mp_obj_type_t machine_neopixel_type;
extern const mp_obj_type_t machine_dht_type;
extern const mp_obj_type_t machine_onewire_type;
extern const mp_obj_type_t machine_ds18x20_type;

extern nvs_handle mpy_nvs_handle;
extern int mpy_repl_stack_size;
extern int mpy_heap_size;

void machine_pins_init(void);
void machine_pins_deinit(void);
void prepareSleepReset(uint8_t hrst, char *msg);

#endif // MICROPY_INCLUDED_ESP32_MODMACHINE_H
