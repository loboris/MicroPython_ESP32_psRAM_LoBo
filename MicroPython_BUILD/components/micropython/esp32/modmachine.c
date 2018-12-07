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

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "rom/ets_sys.h"
#include "rom/rtc.h"
#include "rom/gpio.h"
#include "soc/rtc.h"
#include "soc/uart_reg.h"
#include "soc/timer_group_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/sens_reg.h"
#include "esp_system.h"
#include "soc/dport_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "rom/uart.h"
#include "esp_sleep.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "driver/uart.h"

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
#include "modnetwork.h"

#if MICROPY_PY_MACHINE

//extern uint8_t temprature_sens_read();
extern uint16_t rom_phy_get_vdd33();

// === Global variables ===
bool mpy_use_spiram = false;
nvs_handle mpy_nvs_handle = 0;
machine_rtc_config_t RTC_DATA_ATTR machine_rtc_config = {0};
bool i2s_driver_installed = false;
int mpy_heap_size = CONFIG_MICROPY_HEAP_SIZE * 1024;
int MPY_DEFAULT_STACK_SIZE = 16*1024;
int MPY_MAX_STACK_SIZE = 32*1024;
int MPY_DEFAULT_HEAP_SIZE = 80*1024;
int MPY_MIN_HEAP_SIZE = 48*1024;
int MPY_MAX_HEAP_SIZE = 96*1024;
int hdr_maxlen = 512;
int body_maxlen = 1024;
int ssh2_hdr_maxlen = 512;
int ssh2_body_maxlen = 1024;

// === Variables stored in RTC_SLOW_MEM ===
static uint64_t RTC_DATA_ATTR s_t_wake;
static uint64_t RTC_DATA_ATTR stub_timeout;
static uint64_t RTC_DATA_ATTR stub_timer;
static uint32_t RTC_DATA_ATTR stub_temp;
static uint32_t RTC_DATA_ATTR stub_flag;
static uint16_t RTC_DATA_ATTR stub_timer_inc;
static const char RTC_RODATA_ATTR wake_fmt_str[] = "[%u] info=%u\n";

//-----------------------------------
static void RTC_IRAM_ATTR wake_stub()
{
    // Clear MMU for CPU 0
    _DPORT_REG_WRITE(DPORT_PRO_CACHE_CTRL1_REG, _DPORT_REG_READ(DPORT_PRO_CACHE_CTRL1_REG) | DPORT_PRO_CACHE_MMU_IA_CLR);
    _DPORT_REG_WRITE(DPORT_PRO_CACHE_CTRL1_REG, _DPORT_REG_READ(DPORT_PRO_CACHE_CTRL1_REG) & (~DPORT_PRO_CACHE_MMU_IA_CLR));

    // ROM code has not started yet, so we need to set delay factor used by ets_delay_us first.
    ets_update_cpu_frequency_rom(ets_get_detected_xtal_freq() / 1000000);

    // Update time
    SET_PERI_REG_MASK(RTC_CNTL_TIME_UPDATE_REG, RTC_CNTL_TIME_UPDATE);
    while (GET_PERI_REG_MASK(RTC_CNTL_TIME_UPDATE_REG, RTC_CNTL_TIME_VALID) == 0) {
        ;
    }
    SET_PERI_REG_MASK(RTC_CNTL_INT_CLR_REG, RTC_CNTL_TIME_VALID_INT_CLR);
    // Get current time
    const uint64_t s_t_now = (uint64_t)READ_PERI_REG(RTC_CNTL_TIME0_REG) | (((uint64_t) READ_PERI_REG(RTC_CNTL_TIME1_REG)) << 32);

    // Check reset reason
    if (rtc_get_reset_reason(0) != DEEPSLEEP_RESET) {
        // Not a deepsleep reset, continue booting
        goto do_wakeup;
    }
    // Check wake up cause
    if (REG_GET_FIELD(RTC_CNTL_WAKEUP_STATE_REG, RTC_CNTL_WAKEUP_CAUSE) & RTC_EXT0_TRIG_EN) {
        // === EXT0 wake up ===
        if ((machine_rtc_config.ext0_pin >= 0) && (machine_rtc_config.ext0_count > 0)) {
            if (machine_rtc_config.pulse_count == 0) machine_rtc_config.ext0_last_time = s_t_now;
            else {
                if ((s_t_now - machine_rtc_config.ext0_last_time) > stub_timeout) {
                    machine_rtc_config.pulse_count = 0;
                }
                machine_rtc_config.ext0_last_time = s_t_now;
            }
            // Wait inactive ext0 pin level
            while(1) {
                while (1) {
                    if (machine_rtc_config.ext0_level) stub_flag = (REG_GET_FIELD(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT) & BIT(machine_rtc_config.ext0_rtcpin)) != 0;
                    else stub_flag = (REG_GET_FIELD(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT) & BIT(machine_rtc_config.ext0_rtcpin)) == 0;
                    if (!stub_flag) break;
                    REG_WRITE(TIMG_WDTFEED_REG(0), 1);
                }
                // Debounce, 10 ms
                ets_delay_us(10000);
                REG_WRITE(TIMG_WDTFEED_REG(0), 1);
                if (machine_rtc_config.ext0_level) stub_flag = (REG_GET_FIELD(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT) & BIT(machine_rtc_config.ext0_rtcpin)) != 0;
                else stub_flag = (REG_GET_FIELD(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT) & BIT(machine_rtc_config.ext0_rtcpin)) == 0;
                if (stub_flag) break;
            }
            machine_rtc_config.pulse_count++;
            ets_printf(wake_fmt_str, 0, machine_rtc_config.pulse_count);
            if (machine_rtc_config.pulse_count >= machine_rtc_config.ext0_count) goto do_wakeup;
            ets_delay_us(1000);
            REG_WRITE(TIMG_WDTFEED_REG(0), 1);
            goto do_sleep;
        }
        else goto do_wakeup;
    }

    if (REG_GET_FIELD(RTC_CNTL_WAKEUP_STATE_REG, RTC_CNTL_WAKEUP_CAUSE) & RTC_EXT1_TRIG_EN) {
        // === EXT1 wake up ===
        if (machine_rtc_config.ext1_level == EXT1_WAKEUP_ALL_HIGH) {
            // some of the pins is high, but we want ALL to be high
            stub_flag = 0;
            for(stub_temp = 0; stub_temp < EXT1_WAKEUP_MAX_PINS; stub_temp++) {
                if (machine_rtc_config.ext1_pins[stub_temp] >= 0) stub_flag |= BIT(machine_rtc_config.ext1_rtcpins[stub_temp]);
            }

            if (stub_flag) {
                // Wait for all high
                stub_temp = 0;
                while ((REG_GET_FIELD(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT) & stub_flag) != stub_flag) {
                    ets_delay_us(1000);
                    REG_WRITE(TIMG_WDTFEED_REG(0), 1);
                    if ((REG_GET_FIELD(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT) & stub_flag) == 0)  goto do_sleep;
                    stub_temp++;
                    if (stub_temp > 2000) goto do_sleep;
                }
                goto do_wakeup;
            }
            else goto do_wakeup;
        }
        goto do_wakeup;
    }

    if (!(REG_GET_FIELD(RTC_CNTL_WAKEUP_STATE_REG, RTC_CNTL_WAKEUP_CAUSE) & RTC_TIMER_TRIG_EN)) {
        // Not a timer wake up, continue booting
        goto do_wakeup;
    }

    // === Reset reason: DEEPSLEEP_RESET & Wake up cause: Timer ===
    if ((machine_rtc_config.deepsleep_time) && (machine_rtc_config.deepsleep_interval)) {
        // == Set the out pin to active level if configured
        if (machine_rtc_config.stub_outpin >= 0) {
            gpio_pad_select_gpio(machine_rtc_config.stub_outpin);
            if (machine_rtc_config.stub_outpin < 32)
                gpio_output_set(machine_rtc_config.stub_outpin_level << machine_rtc_config.stub_outpin,
                        (machine_rtc_config.stub_outpin_level ? 0 : 1) << machine_rtc_config.stub_outpin,
                        1<<machine_rtc_config.stub_outpin,0);
            else
                gpio_output_set_high(machine_rtc_config.stub_outpin_level << (machine_rtc_config.stub_outpin-32),
                        (machine_rtc_config.stub_outpin_level ? 0 : 1) << (machine_rtc_config.stub_outpin-32),
                        1<<(machine_rtc_config.stub_outpin-32),0);
        }

        // == Check remaining sleep time
        if (machine_rtc_config.deepsleep_time > machine_rtc_config.deepsleep_interval) {
            machine_rtc_config.deepsleep_time -= machine_rtc_config.deepsleep_interval;
            s_t_wake = s_t_now + machine_rtc_config.wakeup_delay_ticks;
            // Set the pointer of the wake stub function.
            REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&wake_stub);
        }
        else {
            if (machine_rtc_config.wakeup_delay_ticks_last) {
                s_t_wake = s_t_now + machine_rtc_config.wakeup_delay_ticks_last;
                // Next time use the default wake stab
                REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&esp_wake_deep_sleep);
            }
            else {
                REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&esp_wake_deep_sleep);
                return;
            }
        }

        // == Check if we need to wait in wake stub
        if (machine_rtc_config.stub_wait) {
            if (machine_rtc_config.stub_wait < 1000) {
                ets_delay_us(machine_rtc_config.stub_wait);
                REG_WRITE(TIMG_WDTFEED_REG(0), 1);
            }
            else {
                stub_timer = 0;
                while (stub_timer < machine_rtc_config.stub_wait) {
                    ets_delay_us(stub_timer_inc);
                    REG_WRITE(TIMG_WDTFEED_REG(0), 1);
                    stub_timer += stub_timer_inc;
                }
            }
        }

        // == Reset the led pin if configured
        if (machine_rtc_config.stub_outpin >= 0) {
            if (machine_rtc_config.stub_outpin < 32)
                gpio_output_set(machine_rtc_config.stub_outpin_level << machine_rtc_config.stub_outpin,
                        (machine_rtc_config.stub_outpin_level ? 1 : 0) << machine_rtc_config.stub_outpin,
                        1<<machine_rtc_config.stub_outpin,0);
            else
                gpio_output_set_high(machine_rtc_config.stub_outpin_level << (machine_rtc_config.stub_outpin-32),
                        (machine_rtc_config.stub_outpin_level ? 1 : 0) << (machine_rtc_config.stub_outpin-32),
                        1<<(machine_rtc_config.stub_outpin-32),0);
        }
    }
    else goto do_wakeup;

    // === Go back to deepsleep ===
    // Write clock value to RTC:
    WRITE_PERI_REG(RTC_CNTL_SLP_TIMER0_REG, s_t_wake & UINT32_MAX);
    WRITE_PERI_REG(RTC_CNTL_SLP_TIMER1_REG, s_t_wake >> 32);

    do_sleep:
    ets_printf(wake_fmt_str, 88, 88);
    // Wait for UART to end transmitting.
    while (REG_GET_FIELD(UART_STATUS_REG(0), UART_ST_UTX_OUT)) {
        REG_WRITE(TIMG_WDTFEED_REG(0), 1); // feed the watchdog
    }

    REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&wake_stub);
    // Go to sleep.
    CLEAR_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    SET_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);

    // A few CPU cycles may be necessary for the sleep to start...
    while (true) {
        ;
    }
    // never reaches here.

    do_wakeup:
    ets_printf(wake_fmt_str, 99, 99);
    // Wait for UART to end transmitting.
    while (REG_GET_FIELD(UART_STATUS_REG(0), UART_ST_UTX_OUT)) {
        REG_WRITE(TIMG_WDTFEED_REG(0), 1); // feed the watchdog
    }
    REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&wake_stub);
    return;
}


//---------------------------------------------
void prepareSleepReset(uint8_t hrst, char *msg)
{
    // Umount external & internal fs
    externalUmount();
    internalUmount();

    if (!hrst) {
        if (msg) mp_hal_stdout_tx_str(msg);
        /*
        // stop and deinitialize WiFi
        if (wifi_network_state == WIFI_STATE_STARTED) {
            wifi_network_state = WIFI_STATE_STOPPED;
            wifi_sta_isconnected = false;
            wifi_sta_has_ipaddress = false;
            wifi_sta_changed_ipaddress = false;
            wifi_ap_isconnected = false;
            wifi_ap_sta_isconnected = false;
            esp_wifi_stop();
            esp_wifi_deinit();
        }
        */
        // deinitialise peripherals
        //ToDo: deinitialize other peripherals, threads, services, ...
        machine_pins_deinit();

        mp_deinit();
        fflush(stdout);
    }
}

//-----------------------------------------------------------------
STATIC mp_obj_t machine_freq(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        // get CPU frequency
        return mp_obj_new_int(rtc_clk_cpu_freq_value(rtc_clk_cpu_freq_get()));
    }
    else {
        // set CPU frequency
        int freq = mp_obj_get_int(args[0]);
        if (freq > 240) freq /= 1000000;
        rtc_cpu_freq_t max_freq;
        if (!rtc_clk_cpu_freq_from_mhz(freq, &max_freq)) {
            char msg[128];
            sprintf(msg, "Available frequencies: 2MHz, 80Mhz, 160MHz, 240MHz or %uMHz (XTAL)", rtc_clk_xtal_freq_get());
            mp_raise_ValueError(msg);
        }
#ifdef CONFIG_PM_ENABLE
        esp_pm_config_esp32_t pm_config;
        pm_config.max_cpu_freq = max_freq;
        pm_config.min_cpu_freq = RTC_CPU_FREQ_XTAL;
        pm_config.light_sleep_enable = false;
        if (esp_pm_configure(&pm_config) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, "Error configuring frequency");
        }
#endif
        rtc_clk_cpu_freq_set(max_freq);
        uart_set_baudrate(UART_NUM_0, CONFIG_CONSOLE_UART_BAUDRATE);
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

    if (mpy_use_spiram) {
#if CONFIG_SPIRAM_USE_MEMMAP
        mp_printf(&mp_plat_print, "\nSPIRAM info (MEMMAP used):\n--------------------------\n");
        mp_printf(&mp_plat_print, "            Total: %u\n", CONFIG_SPIRAM_SIZE);
        mp_printf(&mp_plat_print, "Used for MPy heap: %u\n", mpy_heap_size);
        mp_printf(&mp_plat_print, "  Free (not used): %u\n", CONFIG_SPIRAM_SIZE - mpy_heap_size);
#else
        mp_printf(&mp_plat_print, "\nSPIRAM info:\n------------\n");
        heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
        print_heap_info(&info);
#endif
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_heap_info_obj, machine_heap_info);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_deepsleep(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum {ARG_sleep_ms, ARG_stub_ms, ARG_stub_led, ARG_stub_ledlevel, ARG_stub_wait};
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_sleep_ms,			MP_ARG_INT,  { .u_int = 0 } },
            { MP_QSTR_stub_ms,			MP_ARG_INT,  { .u_int = 0 } },
            { MP_QSTR_stub_led,			MP_ARG_INT,  { .u_int = -1 } },
            { MP_QSTR_stub_ledlevel,	MP_ARG_BOOL, { .u_bool = false } },
            { MP_QSTR_stub_wait,		MP_ARG_INT,  { .u_int = 0 } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    esp_set_deep_sleep_wake_stub(&esp_wake_deep_sleep);
    //REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&esp_wake_deep_sleep);
    machine_rtc_config.stub_outpin = -1;
    machine_rtc_config.pulse_count = 0;
    machine_rtc_config.deepsleep_interval = 0;
    machine_rtc_config.stub_wait = 0;

    uint32_t s_rtc_clk_cal = (uint64_t)REG_READ(RTC_SLOW_CLK_CAL_REG);
    stub_timeout = (uint64_t)(2000000) * (1 << RTC_CLK_CAL_FRACT) / s_rtc_clk_cal;

    int64_t stub_sleep = 0;
    int64_t sleep_time = args[ARG_sleep_ms].u_int;
    if (sleep_time < 0) sleep_time = 0;
    if (sleep_time > 0) {
        stub_sleep = args[ARG_stub_ms].u_int;
        if (stub_sleep < 0) stub_sleep = 0;
        if (stub_sleep >= sleep_time) stub_sleep = 0;
    }

    int led_pin = args[ARG_stub_led].u_int;
    if ((led_pin < -1) || (led_pin > 34)) {
        mp_raise_ValueError("Wrong led pin !");
    }

    int64_t wait_in_stub = 0;
    if (stub_sleep > 0) {
        wait_in_stub = (int64_t)args[ARG_stub_wait].u_int;
        if (wait_in_stub < 0) wait_in_stub = 0;
        if (wait_in_stub > (stub_sleep*1000)) wait_in_stub = stub_sleep*1000;
        machine_rtc_config.stub_wait = wait_in_stub;
        stub_timer_inc = 1;
        if (wait_in_stub > 100000) stub_timer_inc = 100;
        else stub_timer_inc = 10;
    }

    if (sleep_time > 0) {
        if (stub_sleep) esp_sleep_enable_timer_wakeup(stub_sleep * 1000);
        else esp_sleep_enable_timer_wakeup(sleep_time * 1000);
        machine_rtc_config.deepsleep_time = sleep_time;
    }
    else {
        if ((machine_rtc_config.ext0_pin < 0) && (machine_rtc_config.ext1_pins == 0) && (!machine_rtc_config.wake_on_touch))  {
            mp_raise_ValueError("No other wake-up sources configured, sleep time cannot be 0 !");
        }
    }

    if (machine_rtc_config.ext0_pin >= 0) {
        ESP_LOGD("DEEP SLEEP", "EXT0=%d\n", machine_rtc_config.ext0_pin);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)machine_rtc_config.ext0_pin, machine_rtc_config.ext0_level ? 1 : 0);
        esp_set_deep_sleep_wake_stub(&wake_stub);
    }

    uint64_t ext1_pins = 0;
    for (int i = 0; i < EXT1_WAKEUP_MAX_PINS; i++) {
        if (machine_rtc_config.ext1_pins[i] >= 0) {
            uint64_t pin_bit = (1ll << machine_rtc_config.ext1_pins[i]);
            ext1_pins |= pin_bit;
        }
    }
    if (ext1_pins != 0) {
        ESP_LOGD("DEEP SLEEP", "EXT1 = [%llx]\n", ext1_pins);
        //esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
        uint8_t ext1_level = machine_rtc_config.ext1_level;
        if (machine_rtc_config.ext1_level == EXT1_WAKEUP_ALL_HIGH) ext1_level = ESP_EXT1_WAKEUP_ANY_HIGH;
        esp_sleep_enable_ext1_wakeup(ext1_pins, ext1_level);
        esp_set_deep_sleep_wake_stub(&wake_stub);
    }

    if (machine_rtc_config.wake_on_touch) {
        esp_sleep_enable_touchpad_wakeup();
    }

    ESP_LOGD("DEEP SLEEP", "Sleep time: time=%llu, interval=%llu, pin=%d, level=%d, wait=%llu\n",
            sleep_time, stub_sleep, led_pin, args[ARG_stub_ledlevel].u_bool, wait_in_stub);
    prepareSleepReset(0, NULL);

    if ((stub_sleep) || (led_pin >= 0)) {
        if (led_pin >= 0) {
            gpio_pad_select_gpio(led_pin);
            gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);
            gpio_set_level(led_pin, (uint8_t)args[ARG_stub_ledlevel].u_bool ^ 1);
            machine_rtc_config.stub_outpin = (uint8_t)led_pin;
            machine_rtc_config.stub_outpin_level = (uint8_t)args[ARG_stub_ledlevel].u_bool;
        }
        else machine_rtc_config.stub_outpin = -1;

        if (stub_sleep) {
            // Get number of microseconds per RTC clock tick (scaled by 2^19)
            // Calculate RTC clock value for wakeup
            machine_rtc_config.wakeup_delay_ticks = (stub_sleep * 1000) * (1 << RTC_CLK_CAL_FRACT) / s_rtc_clk_cal;
            machine_rtc_config.wakeup_delay_ticks_last = ((sleep_time % stub_sleep) * 1000) * (1 << RTC_CLK_CAL_FRACT) / s_rtc_clk_cal;;

            // Set the wake stub function
            machine_rtc_config.deepsleep_interval = stub_sleep;
        }
        esp_set_deep_sleep_wake_stub(&wake_stub);
    }

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
    tuple[0] = mp_obj_new_str(reason, strlen(reason));
    mpsleep_get_wake_desc(reason);
    tuple[1] = mp_obj_new_str(reason, strlen(reason));
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
uint64_t random_at_most(uint32_t max) {
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
                strval = mp_obj_new_str(value, strlen(value));
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

//--------------------------------------------------------
static int vprintf_redirected(const char *fmt, va_list ap)
{
    int ret = mp_vprintf(&mp_plat_print, fmt, ap);
    return ret;
}

static vprintf_like_t orig_log_func = NULL;
static vprintf_like_t prev_log_func = NULL;
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
        prev_log_func = esp_log_set_vprintf(orig_log_func);
        orig_log_func = NULL;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_logto_esp_obj, mod_machine_logto_esp);

//-----------------------------------
STATIC mp_obj_t mod_machine_tsens() {
    int temper = 0;

    // --- Using code from esp-idf/components/esp32/test/test_tsens.c ---
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
    temper = GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT, SENS_TSENS_OUT_S);

    // --- Using function from esp-idf/components/esp32/lib/librtc.a ---
    //temper = temprature_sens_read();

    // The returned temperature is in Fahrenheit, convert to Celsius
    float ftemper = (float)(temper - 32) / 1.8;

    mp_obj_t tuple[2];

    tuple[0] = mp_obj_new_int_from_uint(temper);
    tuple[1] = mp_obj_new_float(ftemper);
    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_tsens_obj, mod_machine_tsens);

//------------------------------------
STATIC mp_obj_t mod_machine_vdd33() {
    uint16_t val = rom_phy_get_vdd33();

    return mp_obj_new_int(val);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_vdd33_obj, mod_machine_vdd33);

//--------------------------------------------------------------
STATIC mp_obj_t mod_machine_stdin_disable(mp_obj_t pattern_in) {
    bool has_pattern = false;
    mp_buffer_info_t pattern_buff;
    mp_obj_type_t *type = mp_obj_get_type(pattern_in);
    char pattern[16] = {'\0'};
    if (type->buffer_p.get_buffer != NULL) {
        int ret = type->buffer_p.get_buffer(pattern_in, &pattern_buff, MP_BUFFER_READ);
        if (ret == 0) {
            if ((pattern_buff.len > 0) && (pattern_buff.len < 16)) has_pattern = true;
        }
    }
    if (!has_pattern) {
        mp_raise_ValueError("invalid pattern (15 chars allowed)");
    }

    memcpy(pattern, pattern_buff.buf, pattern_buff.len);
    disableStdin(pattern);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_machine_stdin_disable_obj, mod_machine_stdin_disable);

//---------------------------------------
STATIC mp_obj_t mod_machine_reset_wdt() {
    mp_hal_reset_wdt();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_reset_wdt_obj, mod_machine_reset_wdt);

//-------------------------------------
STATIC mp_obj_t mod_machine_set_wdt() {
    mp_hal_set_wdt_tmo();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_machine_set_wdt_obj, mod_machine_set_wdt);

//--------------------------------------------------------------------
STATIC mp_obj_t mod_machine_wdt(size_t n_args, const mp_obj_t *args) {
    #ifdef CONFIG_MICROPY_USE_TASK_WDT
    esp_err_t res;
    res = esp_task_wdt_status(NULL);
    if (n_args > 0) {
        if ((mp_obj_is_true(args[0])) && (res != ESP_OK)) esp_task_wdt_add(NULL);
        else if ((!mp_obj_is_true(args[0])) && (res == ESP_OK)) esp_task_wdt_delete(NULL);
    }
    res = esp_task_wdt_status(NULL);
    if (res == ESP_OK) return mp_const_true;
    return mp_const_false;
    #else
    return mp_const_false;
    #endif
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_machine_wdt_obj, 0, 1, mod_machine_wdt);

//-----------------------------------------------------------------------
static void _set_stack_heap(char *key, int value, int valmin, int valmax)
{
    checkNVS();

    if ((value != 0) && ((value < valmin) || (value > valmax))) {
        mp_raise_msg(&mp_type_OSError, "Invalid size");
    }

    esp_err_t esp_err = nvs_set_i32(mpy_nvs_handle, key, value);
    if (ESP_OK == esp_err) {
        nvs_commit(mpy_nvs_handle);
    }
    else if (ESP_ERR_NVS_NOT_ENOUGH_SPACE == esp_err || ESP_ERR_NVS_PAGE_FULL == esp_err || ESP_ERR_NVS_NO_FREE_PAGES == esp_err) {
        mp_raise_msg(&mp_type_OSError, "No space available for NVS variable.");
    }
    else if (ESP_ERR_NVS_INVALID_NAME == esp_err || ESP_ERR_NVS_KEY_TOO_LONG == esp_err) {
        mp_raise_msg(&mp_type_OSError, "NVS Key invalid or too long");
    }
}

//----------------------------------------------------------
STATIC mp_obj_t mod_machine_set_stack_size (mp_obj_t _value)
{
    int value = mp_obj_get_int_truncated(_value);
    value &= 0x7FFFFFFC;
    _set_stack_heap("MPY_StackSize", value, MPY_MIN_STACK_SIZE, MPY_MAX_STACK_SIZE);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_machine_set_stack_size_obj, mod_machine_set_stack_size);

//-----------------------------------------------------------
STATIC mp_obj_t mod_machine_set_heap_size (mp_obj_t _value)
{
    int value = mp_obj_get_int_truncated(_value);
    value &= 0x7FFFFFFC;
    _set_stack_heap("MPY_HeapSize", value, MPY_MIN_HEAP_SIZE, MPY_MAX_HEAP_SIZE);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_machine_set_heap_size_obj, mod_machine_set_heap_size);


//===============================================================
STATIC const mp_rom_map_elem_t machine_module_globals_table[] = {
        { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_umachine) },

        { MP_ROM_QSTR(MP_QSTR_mem8),					MP_ROM_PTR(&machine_mem8_obj) },
        { MP_ROM_QSTR(MP_QSTR_mem16),					MP_ROM_PTR(&machine_mem16_obj) },
        { MP_ROM_QSTR(MP_QSTR_mem32),					MP_ROM_PTR(&machine_mem32_obj) },

        { MP_ROM_QSTR(MP_QSTR_freq),					MP_ROM_PTR(&machine_freq_obj) },
        { MP_ROM_QSTR(MP_QSTR_reset),					MP_ROM_PTR(&machine_reset_obj) },
        { MP_ROM_QSTR(MP_QSTR_resetWDT),				MP_ROM_PTR(&mod_machine_reset_wdt_obj) },
        { MP_ROM_QSTR(MP_QSTR_setWDT),					MP_ROM_PTR(&mod_machine_set_wdt_obj) },
        { MP_ROM_QSTR(MP_QSTR_WDT),                     MP_ROM_PTR(&mod_machine_wdt_obj) },
        { MP_ROM_QSTR(MP_QSTR_unique_id),				MP_ROM_PTR(&machine_unique_id_obj) },
        { MP_ROM_QSTR(MP_QSTR_idle),					MP_ROM_PTR(&machine_idle_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_deepsleep),			MP_ROM_PTR(&machine_deepsleep_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_wake_reason),			MP_ROM_PTR(&machine_wake_reason_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_wake_description),	MP_ROM_PTR(&machine_wake_desc_obj) },
        { MP_ROM_QSTR(MP_QSTR_heap_info),				MP_ROM_PTR(&machine_heap_info_obj) },
        { MP_ROM_QSTR(MP_QSTR_stdin_disable),			MP_ROM_PTR(&mod_machine_stdin_disable_obj) },
        { MP_ROM_QSTR(MP_QSTR_SetStackSize),			MP_ROM_PTR(&mod_machine_set_stack_size_obj) },
        { MP_ROM_QSTR(MP_QSTR_SetHeapSize),				MP_ROM_PTR(&mod_machine_set_heap_size_obj) },

        { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_setint),			MP_ROM_PTR(&mod_machine_nvs_set_int_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_getint),			MP_ROM_PTR(&mod_machine_nvs_get_int_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_setstr),			MP_ROM_PTR(&mod_machine_nvs_set_str_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_getstr),			MP_ROM_PTR(&mod_machine_nvs_get_str_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_erase),			MP_ROM_PTR(&mod_machine_nvs_erase_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_nvs_erase_all),		MP_ROM_PTR(&mod_machine_nvs_erase_all_obj) },

        { MP_OBJ_NEW_QSTR(MP_QSTR_loglevel),			MP_ROM_PTR(&mod_machine_log_level_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_redirectlog),			MP_ROM_PTR(&mod_machine_logto_mp_obj) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_restorelog),			MP_ROM_PTR(&mod_machine_logto_esp_obj) },

        { MP_ROM_QSTR(MP_QSTR_stdin_get),				MP_ROM_PTR(&machine_stdin_get_obj) },
        { MP_ROM_QSTR(MP_QSTR_stdout_put),				MP_ROM_PTR(&machine_stdout_put_obj) },

        { MP_ROM_QSTR(MP_QSTR_disable_irq),				MP_ROM_PTR(&machine_disable_irq_obj) },
        { MP_ROM_QSTR(MP_QSTR_enable_irq),				MP_ROM_PTR(&machine_enable_irq_obj) },

        { MP_ROM_QSTR(MP_QSTR_time_pulse_us),			MP_ROM_PTR(&machine_time_pulse_us_obj) },

        { MP_ROM_QSTR(MP_QSTR_random),					MP_ROM_PTR(&machine_random_obj) },
        { MP_ROM_QSTR(MP_QSTR_internal_temp),			MP_ROM_PTR(&mod_machine_tsens_obj) },
        { MP_ROM_QSTR(MP_QSTR_internal_vdd),            MP_ROM_PTR(&mod_machine_vdd33_obj) },

        { MP_ROM_QSTR(MP_QSTR_Timer),					MP_ROM_PTR(&machine_timer_type) },
        { MP_ROM_QSTR(MP_QSTR_Pin),						MP_ROM_PTR(&machine_pin_type) },
        { MP_ROM_QSTR(MP_QSTR_Signal),					MP_ROM_PTR(&machine_signal_type) },
        { MP_ROM_QSTR(MP_QSTR_TouchPad),				MP_ROM_PTR(&machine_touchpad_type) },
        { MP_ROM_QSTR(MP_QSTR_ADC),						MP_ROM_PTR(&machine_adc_type) },
        { MP_ROM_QSTR(MP_QSTR_DAC),						MP_ROM_PTR(&machine_dac_type) },
        { MP_ROM_QSTR(MP_QSTR_I2C),						MP_ROM_PTR(&machine_hw_i2c_type) },
        { MP_ROM_QSTR(MP_QSTR_PWM),						MP_ROM_PTR(&machine_pwm_type) },
        { MP_ROM_QSTR(MP_QSTR_DEC),						MP_ROM_PTR(&machine_dec_type) },
        { MP_ROM_QSTR(MP_QSTR_SPI),						MP_ROM_PTR(&machine_hw_spi_type) },
        { MP_ROM_QSTR(MP_QSTR_UART),					MP_ROM_PTR(&machine_uart_type) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_RTC),					MP_ROM_PTR(&mach_rtc_type) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_Neopixel),			MP_ROM_PTR(&machine_neopixel_type) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_DHT),					MP_ROM_PTR(&machine_dht_type) },
        { MP_OBJ_NEW_QSTR(MP_QSTR_Onewire),				MP_ROM_PTR(&machine_onewire_type) },
#ifdef CONFIG_MICROPY_USE_GPS
        { MP_OBJ_NEW_QSTR(MP_QSTR_GPS),					MP_ROM_PTR(&machine_gps_type) },
#endif
#ifdef CONFIG_MICROPY_USE_RFCOMM
        { MP_OBJ_NEW_QSTR(MP_QSTR_RFCOMM),              MP_ROM_PTR(&machine_rfcomm_type) },
#endif
        // Constants
        { MP_ROM_QSTR(MP_QSTR_LOG_NONE),				MP_ROM_INT(ESP_LOG_NONE) },
        { MP_ROM_QSTR(MP_QSTR_LOG_ERROR),				MP_ROM_INT(ESP_LOG_ERROR) },
        { MP_ROM_QSTR(MP_QSTR_LOG_WARN),				MP_ROM_INT(ESP_LOG_WARN) },
        { MP_ROM_QSTR(MP_QSTR_LOG_INFO),				MP_ROM_INT(ESP_LOG_INFO) },
        { MP_ROM_QSTR(MP_QSTR_LOG_DEBUG),				MP_ROM_INT(ESP_LOG_DEBUG) },
        { MP_ROM_QSTR(MP_QSTR_LOG_VERBOSE),				MP_ROM_INT(ESP_LOG_VERBOSE) },
        { MP_ROM_QSTR(MP_QSTR_EXT1_ANYHIGH),			MP_ROM_INT(ESP_EXT1_WAKEUP_ANY_HIGH) },
        { MP_ROM_QSTR(MP_QSTR_EXT1_ALLLOW),				MP_ROM_INT(ESP_EXT1_WAKEUP_ALL_LOW) },
        { MP_ROM_QSTR(MP_QSTR_EXT1_ANYLOW),				MP_ROM_INT(EXT1_WAKEUP_ALL_HIGH) },
};
STATIC MP_DEFINE_CONST_DICT(machine_module_globals, machine_module_globals_table);

//=========================================
const mp_obj_module_t mp_module_machine = {
        .base = { &mp_type_module },
        .globals = (mp_obj_dict_t*)&machine_module_globals,
};

#endif // MICROPY_PY_MACHINE
