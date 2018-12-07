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
#define EXT1_WAKEUP_ALL_HIGH	2           //!< Wake the chip when all selected GPIOs go high
#define EXT1_WAKEUP_MAX_PINS	4
#define ADC_TIMER_NUM			3	        // Timer used in ADC module
#define ADC_TIMER_DIVIDER       8           // 0.1 us per tick, 10 MHz
#define ADC_TIMER_FREQ          10000000.0  //Timer frequency

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

typedef struct _machine_pin_obj_t {
    mp_obj_base_t base;
    gpio_num_t id;
    uint8_t mode;
    uint8_t pull;
    uint8_t irq_type;
    int8_t irq_retvalue;
    uint8_t irq_any_level;
    int32_t irq_debounce;
    int32_t irq_active_time;
    mp_obj_t irq_handler;
} machine_pin_obj_t;

typedef struct _machine_timer_obj_t {
    mp_obj_base_t base;
    uint8_t id;
    uint8_t state;
    uint8_t type;
    int8_t debug_pin;
    int8_t debug_pin_mode;
    mp_uint_t repeat;
    mp_uint_t period;
    uint64_t event_num;
    uint64_t cb_num;
    mp_obj_t callback;
    intr_handle_t handle;
    uint64_t counter;
    uint64_t alarm;
} machine_timer_obj_t;

extern bool mpy_use_spiram;
extern int MPY_DEFAULT_STACK_SIZE;
extern int MPY_MAX_STACK_SIZE;
extern int MPY_DEFAULT_HEAP_SIZE;
extern int MPY_MIN_HEAP_SIZE;
extern int MPY_MAX_HEAP_SIZE;
extern int hdr_maxlen;
extern int body_maxlen;
extern int ssh2_hdr_maxlen;
extern int ssh2_body_maxlen;

extern machine_rtc_config_t machine_rtc_config;
extern machine_timer_obj_t * mpy_timers_used[4];
extern bool adc_timer_active;
extern bool i2s_driver_installed;

extern const mp_obj_type_t machine_timer_type;
extern const mp_obj_type_t machine_pin_type;
extern const mp_obj_type_t machine_touchpad_type;
extern const mp_obj_type_t machine_adc_type;
extern const mp_obj_type_t machine_dac_type;
extern const mp_obj_type_t machine_pwm_type;
extern const mp_obj_type_t machine_dec_type;
extern const mp_obj_type_t machine_hw_spi_type;
extern const mp_obj_type_t machine_hw_i2c_type;
extern const mp_obj_type_t machine_uart_type;
extern const mp_obj_type_t machine_neopixel_type;
extern const mp_obj_type_t machine_dht_type;
extern const mp_obj_type_t machine_onewire_type;
extern const mp_obj_type_t machine_ds18x20_type;
#ifdef CONFIG_MICROPY_USE_RFCOMM
extern const mp_obj_type_t machine_rfcomm_type;
#endif
#ifdef CONFIG_MICROPY_USE_GPS
extern const mp_obj_type_t machine_gps_type;
#endif
extern nvs_handle mpy_nvs_handle;
extern int mpy_heap_size;

void machine_pins_init(void);
void machine_pins_deinit(void);
void prepareSleepReset(uint8_t hrst, char *msg);
int machine_pin_get_gpio(mp_obj_t pin_in);
uint64_t random_at_most(uint32_t max);

#endif // MICROPY_INCLUDED_ESP32_MODMACHINE_H
