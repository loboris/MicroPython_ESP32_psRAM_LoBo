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


#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/timer.h"
#include "driver/i2s.h"
#include "esp_adc_cal.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "esp_log.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"
#include "py/objarray.h"
#include "extmod/vfs_native.h"

#define ADC1_CHANNEL_HALL	ADC1_CHANNEL_MAX
#define I2S_RD_BUF_SIZE     (1024*2)

typedef struct _madc_obj_t {
    mp_obj_base_t base;
    int gpio_id;
    adc_unit_t adc_num;
    adc1_channel_t adc_chan;
    adc_atten_t atten;
    adc_bits_width_t width;
    mp_obj_t callback;
    void *buffer;
    FILE *fhndl;
    uint8_t val_shift;
    size_t buf_len;
    size_t buf_ptr;
    int64_t interval;
    int64_t summ;
    int64_t rms_summ;
    int min;
    int max;
    uint8_t cal_read;
} madc_obj_t;

extern int MainTaskCore;

bool adc_timer_active = false;
bool collect_active = false;
intr_handle_t adc_timer_handle = NULL;

static uint16_t adc1_chan_used = 0;
static uint16_t adc2_chan_used = 0;
static int8_t adc_width = -1;
static int8_t last_adc_width = -1;
static int8_t last_adc_num = -1;
static uint32_t adc_vref = 1100;
static uint32_t last_adc_vref = 0;
static adc_atten_t last_atten = ADC_ATTEN_MAX;
static adc_atten_t last_atten2 = ADC_ATTEN_MAX;
static esp_adc_cal_characteristics_t characteristics;
static uint64_t collect_start_time = 0;
static uint64_t collect_end_time = 0;
static bool task_running = false;
static bool task_stop = false;

static const uint8_t adc1_gpios[ADC1_CHANNEL_MAX] = {36, 37, 38, 39, 32, 33, 34, 35};
static const uint8_t adc2_gpios[ADC2_CHANNEL_MAX] = {4, 0, 2, 15, 13, 12, 14, 27, 25, 26};


//-------------------------------------
static void set_width(madc_obj_t *self)
{
    if (adc_width != self->width) {
        if (self->adc_num == ADC_UNIT_1) {
            esp_err_t err = adc1_config_width(self->width);
            if (err != ESP_OK) mp_raise_ValueError("Set width Error");
        }
        else {
            adc_set_data_width(self->adc_num, self->width);
        }
        adc_width = self->width;
    }
}

//-----------------------------------------------------
static int get_adc_channel(adc_unit_t adc_num, int pin)
{
    int channel = -1;
    if (adc_num == ADC_UNIT_1) {
        for (int i=0; i < ADC1_CHANNEL_MAX; i++) {
            if (adc1_gpios[i] == pin) {
                channel = i;
                break;
            }
        }
    }
    else {
        for (int i=0; i < ADC2_CHANNEL_MAX; i++) {
            if (adc2_gpios[i] == pin) {
                channel = i;
                break;
            }
        }
    }
    return channel;
}

//======================================
static void adc_task(void *pvParameters)
{
    task_running = true;
    madc_obj_t *self = (madc_obj_t *)pvParameters;
    uint8_t *buff8 = NULL;
    uint16_t *buff16 = NULL;
    //char* i2s_read_buff = NULL;
    uint16_t *i2s_read_buff = NULL;
    int arr_idx = 0;
    size_t bytes_read;
    //ESP_LOGE("ADC", "To file: %s, len=%d, shift=%d", self->fhndl ? "True" : "False", self->buf_len, self->val_shift);

    if (self->fhndl) {
        // Allocate temporary data buffer
        if (self->val_shift) buff8 = malloc(I2S_RD_BUF_SIZE/2);
        else buff16 = malloc(I2S_RD_BUF_SIZE);
        if ((buff8 == NULL) && (buff16 == NULL)) {
            fclose(self->fhndl);
            ESP_LOGE("ADC", "Error allocating adc buffer");
            goto exit;
        }
    }
    else {
        if (self->val_shift) buff8 = (uint8_t *)self->buffer;
        else buff16 = (uint16_t *)self->buffer;
    }
    // allocate i2s read buffer
    i2s_read_buff = calloc(I2S_RD_BUF_SIZE, 1);
    if (i2s_read_buff == NULL) {
        if (self->fhndl) {
            fclose(self->fhndl);
            if (buff8) free(buff8);
            if (buff16) free(buff16);
        }
        ESP_LOGE("ADC", "Error allocating i2s read buffer");
        goto exit;
    }

    self->buf_ptr = 0;
    collect_start_time = esp_timer_get_time(); //mp_hal_ticks_us();
    collect_end_time = collect_start_time;

    // read ADC data
    while (self->buf_ptr < self->buf_len) {
        // read data from I2S bus, in this case, from ADC.
        i2s_read(0, (void *)i2s_read_buff, I2S_RD_BUF_SIZE, &bytes_read, 1000);
        if (bytes_read != I2S_RD_BUF_SIZE) {
            ESP_LOGE("ADC", "I2S error reading (%d)", bytes_read);
            break;
        }
        // save read ADC values to the output buffer
        for (int i=0; i<(I2S_RD_BUF_SIZE/2); i++) {
            if (self->buf_ptr < self->buf_len) {
                uint16_t val = i2s_read_buff[i] & 0x0fff;
                if (self->val_shift) buff8[arr_idx++] = (uint8_t)(val >> self->val_shift);
                else buff16[arr_idx++] = val;
                self->buf_ptr++;
            }
        }
        if ( (self->fhndl) && ( (arr_idx >= (I2S_RD_BUF_SIZE/2)) || (self->buf_ptr < self->buf_len) ) ) {
            // save buffer to file
            int res;
            if (self->val_shift) res = fwrite(buff8, 1, arr_idx, self->fhndl);
            else res = fwrite(buff16, 2, arr_idx, self->fhndl);
            if (res != arr_idx) {
                ESP_LOGE("ADC", "Error writing to file at %d", arr_idx);
                break;
            }
            arr_idx = 0;
        }
        //#if CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU0 || CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1
        //vTaskDelay(0); // allow other core idle task to reset the watchdog
        //#endif
    }
    collect_end_time = esp_timer_get_time(); //mp_hal_ticks_us();

    if (self->fhndl) {
        // reading to file, close file and free the buffer
        fclose(self->fhndl);
        self->fhndl = NULL;
        if (buff8) free(buff8);
        if (buff16) free(buff16);
    }

    if (self->callback) mp_sched_schedule(self->callback, self, NULL);

exit:
    // i2s cleanup
    i2s_adc_disable(0);
    i2s_driver_uninstall(0);
    i2s_driver_installed = false;
    if (i2s_read_buff) free(i2s_read_buff);

    esp_log_level_set("I2S", CONFIG_LOG_DEFAULT_LEVEL);
    task_stop = false;
    task_running = false;

    vTaskDelete(NULL);
}

//======================================
// ADC Timer interrupt function
//======================================
STATIC void adc_timer_isr(void *self_in)
{
    madc_obj_t *self = (madc_obj_t *)self_in;

    // Clear timer interrupt
    if (ADC_TIMER_NUM & 2) {
        if (ADC_TIMER_NUM & 1) TIMERG1.int_clr_timers.t1 = 1;
        else TIMERG1.int_clr_timers.t0 = 1;
    }
    else {
        if (ADC_TIMER_NUM & 1) TIMERG0.int_clr_timers.t1 = 1;
        else TIMERG0.int_clr_timers.t0 = 1;
    }

    uint16_t *buffer16 = (uint16_t *)self->buffer;
    uint8_t *buffer8 = (uint8_t *)self->buffer;

    if (self->buf_ptr == 0) collect_start_time = esp_timer_get_time(); //mp_hal_ticks_us();

    // --- Read ADC value ---
    int val = 0;
    esp_err_t err = ESP_OK;
    if (self->cal_read) {
        err = esp_adc_cal_get_voltage(self->adc_chan, &characteristics, (uint32_t *)&val);
    }
    else {
        if (self->adc_num == ADC_UNIT_1) {
            val = adc1_get_raw(self->adc_chan);
            if (val == -1) err = ESP_FAIL;
        }
        else err = adc2_get_raw(self->adc_chan, self->atten, &val);	//if (err != ESP_OK) : Cannot read, ADC2 used by Wi-Fi"
    }

    // --- Calculate values & save to buffer if provided ---
    if (err == ESP_OK) {
        if (self->buf_ptr < self->buf_len) {
            if (self->buffer) {
                // store value in buffer
                if (self->val_shift == 0) buffer16[self->buf_ptr] = (uint16_t)val;
                else {
                    if (self->cal_read) {
                        // calibrated read (mV) converted to 8-bit value
                        if (val > 2500) buffer8[self->buf_ptr] = 255;
                        else buffer8[self->buf_ptr] = val / 10;
                    }
                    else buffer8[self->buf_ptr] = (uint8_t)(val >> self->val_shift);
                }
            }
            self->buf_ptr++;
            self->summ += val;
            self->rms_summ += (val * val);
            if (val < self->min) self->min = val;
            if (val > self->max) self->max = val;
        }
    }

    if ((err != ESP_OK) || (self->buf_ptr >= self->buf_len)) {
        // --- Finished, all data read or ADC read error ---
        timer_disable_intr((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        timer_pause((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        if (adc_timer_handle) {
            esp_intr_free(adc_timer_handle);
            adc_timer_handle = NULL;
        }
        collect_end_time = esp_timer_get_time(); //mp_hal_ticks_us();
        if (self->callback) mp_sched_schedule(self->callback, self, NULL);
        self->buffer = NULL;
        adc_timer_active = false;
        collect_active = false;
    }
    else {
        // --- Not yet finished, enable alarm interrupt ---
        if (ADC_TIMER_NUM & 2) TIMERG1.hw_timer[ADC_TIMER_NUM & 1].config.alarm_en = true;
        else TIMERG0.hw_timer[ADC_TIMER_NUM & 1].config.alarm_en = true;
    }
}

// Initialize and start the timer for ADC data collection
//--------------------------------------------------------------
STATIC esp_err_t start_adc_timer(madc_obj_t *adc_obj, bool wait)
{
    if (adc_timer_handle) {
        esp_intr_free(adc_timer_handle);
        adc_timer_handle = NULL;
    }

    adc_obj->summ = 0;
    adc_obj->rms_summ = 0;
    adc_obj->min = 999999;
    adc_obj->max = -999999;
    collect_start_time = 0;
    collect_end_time = 0;

    esp_err_t err = ESP_OK;
    // Set width if needed
    if (adc_width != adc_obj->width) {
        if (adc_obj->adc_num == ADC_UNIT_1) err = adc1_config_width(adc_obj->width);
        else err = adc_set_data_width(adc_obj->adc_num, adc_obj->width);
        adc_width = adc_obj->width;
        if (err != ESP_OK) return err;
    }
    if (adc_obj->adc_num == ADC_UNIT_2) {
        if (last_atten2 != adc_obj->atten) {
            err = adc2_config_channel_atten(adc_obj->adc_chan, adc_obj->atten);
            last_atten2 = adc_obj->atten;
        }
        if (err != ESP_OK) return err;
    }
    if (adc_obj->cal_read) {
        if ((last_adc_num != adc_obj->adc_num) || (last_adc_vref != adc_vref) || (last_atten != adc_obj->atten) || (last_adc_width != adc_obj->width)) {
            // New characterization needed
            esp_adc_cal_characterize(adc_obj->adc_num, adc_obj->atten, adc_obj->width, adc_vref, &characteristics);
            adc_vref = characteristics.vref;
            last_adc_vref = adc_vref;
            last_atten = adc_obj->atten;
            last_adc_width = adc_obj->width;
            last_adc_num = adc_obj->adc_num;
        }
    }

    timer_config_t config;
    config.counter_dir = TIMER_COUNT_UP;
    config.intr_type = TIMER_INTR_LEVEL;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.auto_reload = true;
    config.divider = ADC_TIMER_DIVIDER;

    err = timer_init((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, &config);
    if (err != ESP_OK) return err;

    // Timer's counter will initially start from value below.
    // Also, if auto_reload is set, this value will be automatically reload on alarm

    err = timer_set_counter_value((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, 0x00000000ULL);
    if (err != ESP_OK) return err;

    // Configure the alarm value and the interrupt on alarm.
    err = timer_set_alarm_value((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, adc_obj->interval);
    if (err != ESP_OK) return err;

    // Enable timer interrupt
    err = timer_enable_intr((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
    if (err != ESP_OK) return err;

    // Register interrupt callback
    err = timer_isr_register((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, adc_timer_isr, (void*)adc_obj, ESP_INTR_FLAG_LEVEL1, &adc_timer_handle);
    if (err != ESP_OK) return err;

    // Start the timer
    err = timer_start((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
    if (err != ESP_OK) {
        timer_pause((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        if (adc_timer_handle) {
            esp_intr_free(adc_timer_handle);
            adc_timer_handle = NULL;
        }
        return err;
    }

    if (wait) {
        while (collect_active) {
            mp_hal_delay_ms(5);
        }
    }
    return ESP_OK;
}

//------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t madc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum { ARG_pin, ARG_unit };
    static const mp_arg_t allowed_args[] = {
            { MP_QSTR_pin,	MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none}},
            { MP_QSTR_unit,	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = ADC_UNIT_1}},
    };
    // parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    esp_err_t err = 0;

    mp_arg_check_num(n_args, n_kw, 1, 1, true);
    int pin_id = 0;

    pin_id = machine_pin_get_gpio(args[ARG_pin].u_obj);

    madc_obj_t *self = m_new_obj(madc_obj_t);
    self->base.type = &machine_adc_type;

    self->buffer = NULL;
    self->buf_len = 0;
    self->buf_ptr = 0;
    self->interval = 0;
    self->cal_read = 0;
    self->callback = NULL;

    self->adc_num = args[ARG_unit].u_int;
    if ((self->adc_num != 0) && (self->adc_num != ADC_UNIT_1) && (self->adc_num != ADC_UNIT_2)) {
        mp_raise_ValueError("invalid ADC unit (1 and 2 allowed)");
    }
    self->atten = ADC_ATTEN_DB_0;
    self->width = ADC_WIDTH_BIT_12;

    if (pin_id != ADC1_CHANNEL_HALL) {
        int channel = -1;
        if (self->adc_num == 0) {
            channel = get_adc_channel(ADC_UNIT_1, pin_id);
            if (channel >= 0) self->adc_num = ADC_UNIT_1;
            else {
                channel = get_adc_channel(ADC_UNIT_2, pin_id);
                if (channel >= 0) self->adc_num = ADC_UNIT_2;
            }
        }
        else channel = get_adc_channel(self->adc_num, pin_id);
        if (channel < 0) mp_raise_ValueError("invalid Pin for ADC");

        self->adc_chan = channel;
        self->gpio_id = pin_id;

        if (self->adc_num == ADC_UNIT_1) {
            if ((adc1_chan_used & 0x0100) && ((pin_id == 36) || (pin_id == 39))) mp_raise_ValueError("hall used, cannot use pins 36 & 39");
            if (adc1_chan_used & (1 << self->adc_chan)) mp_raise_ValueError("pin already used for adc");
            adc1_chan_used |= (1 << self->adc_chan);

            err = adc_gpio_init(self->adc_num, self->adc_chan);
            if (err != ESP_OK) mp_raise_ValueError("Error configuring ADC gpio");
            err = adc1_config_channel_atten(self->adc_chan, ADC_ATTEN_DB_0);
            if (err != ESP_OK) mp_raise_ValueError("Error configuring attenuation");
        }
        else {
            if (adc2_chan_used & (1 << self->adc_chan)) mp_raise_ValueError("pin already used for adc");
            adc2_chan_used |= (1 << self->adc_chan);

            gpio_pad_select_gpio(self->gpio_id);
            gpio_set_direction(self->gpio_id, GPIO_MODE_DISABLE);
            gpio_set_pull_mode(self->gpio_id, GPIO_FLOATING);

            adc_gpio_init(self->adc_num, self->adc_chan);
            if (err != ESP_OK) mp_raise_ValueError("Error configuring ADC gpio");
            if (last_atten2 != self->atten) {
                adc2_config_channel_atten(self->adc_chan, self->atten);
                last_atten2 = self->atten;
            }
        }
    }
    else {
        self->adc_num = ADC_UNIT_1;
        if (adc1_chan_used & 0x09) mp_raise_ValueError("adc on gpio 36 or 39 used");
        if (adc1_chan_used & 0x0100) mp_raise_ValueError("hall already used");
        adc1_chan_used |= 0x0100;
        self->adc_chan = ADC1_CHANNEL_HALL;
        self->gpio_id = GPIO_NUM_MAX;
    }

    set_width(self);

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------------------
static void _is_init(madc_obj_t *self, bool iflag, bool cflag)
{
    if ((iflag) && (self->gpio_id < 0)) {
        mp_raise_ValueError("Not initialized");
    }
    if ((cflag) && (collect_active | task_running)) {
        mp_raise_ValueError("collecting data in progress");
    }
    if ((cflag) && (adc_timer_active)) {
        mp_raise_ValueError("ADC timer used by other module");
    }
}

//-------------------------------------------
STATIC mp_obj_t madc_deinit(mp_obj_t self_in)
{
    madc_obj_t *self = self_in;
    _is_init(self, false, true);

    if (self->gpio_id < 0) return mp_const_none;

    if (self->adc_num == ADC_UNIT_1) {
        if (self->adc_chan == ADC1_CHANNEL_HALL) {
            adc1_chan_used &= 0x00FF;
            gpio_pad_select_gpio(36);
            gpio_pad_select_gpio(39);
        }
        else {
            adc1_chan_used &= (~(1 << self->adc_chan) & 0x1FF);
            gpio_pad_select_gpio(self->gpio_id);
            gpio_set_direction(self->gpio_id, GPIO_MODE_INPUT);
            gpio_set_pull_mode(self->gpio_id, GPIO_FLOATING);
        }
    }
    else {
        adc2_chan_used &= (~(1 << self->adc_chan) & 0x3FF);
        gpio_pad_select_gpio(self->gpio_id);
        gpio_set_direction(self->gpio_id, GPIO_MODE_INPUT);
        gpio_set_pull_mode(self->gpio_id, GPIO_FLOATING);
    }
    self->gpio_id = -1;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_deinit_obj, madc_deinit);

//---------------------------------------------------------------------------------------
STATIC void madc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    madc_obj_t *self = self_in;

    if (self->gpio_id < 0) {
        mp_printf(print, "ADC( deinitialized )");
        return;
    }

    char satten[16];
    char spin[8];
    if (self->atten == ADC_ATTEN_DB_0) sprintf(satten, "0dB (1.1V)");
    else if (self->atten == ADC_ATTEN_DB_2_5) sprintf(satten, "2.5dB (1.5V)");
    else if (self->atten == ADC_ATTEN_DB_6) sprintf(satten, "6dB (2.5V)");
    else if (self->atten == ADC_ATTEN_DB_11) sprintf(satten, "11dB (3.9V)");
    else sprintf(satten, "Unknown");

    if (self->gpio_id == GPIO_NUM_MAX) sprintf(spin, "HALL");
    else sprintf(spin, "Pin(%u)", self->gpio_id);

    mp_printf(print, "ADC(%s: unit=ADC%d, chan=%d, width=%u bits, atten=%s, Vref=%u mV)", spin, self->adc_num, self->adc_chan, self->width+9, satten, adc_vref);
}

//----------------------------------------------
STATIC mp_obj_t madc_readraw(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
    _is_init(self, true, true);

    int val = 0;
    if (self->adc_num == ADC_UNIT_1) {
        set_width(self);

        if (self->gpio_id == GPIO_NUM_MAX) val= hall_sensor_read();
        else val = adc1_get_raw(self->adc_chan);
        if (val == -1) mp_raise_ValueError("Parameter Error (ADC raw read)");
    }
    else {
        if (last_atten2 != self->atten) {
            adc2_config_channel_atten(self->adc_chan, self->atten);
            last_atten2 = self->atten;
        }
        esp_err_t err = adc2_get_raw(self->adc_chan, self->atten, &val);
        if (err != ESP_OK) mp_raise_ValueError("Cannot read, ADC2 used by Wi-Fi");
    }

    return MP_OBJ_NEW_SMALL_INT(val);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_readraw_obj, madc_readraw);

//-------------------------------------------
STATIC mp_obj_t madc_read(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
    _is_init(self, true, true);

    set_width(self);

    int adc_val = 0;
    if (self->gpio_id == GPIO_NUM_MAX) adc_val= hall_sensor_read();
    else {
        if ((last_adc_num != self->adc_num) || (last_adc_vref != adc_vref) || (last_atten != self->atten) || (last_adc_width != self->width)) {
            // New characterization needed
            esp_adc_cal_characterize(self->adc_num, self->atten, self->width, adc_vref, &characteristics);
            adc_vref = characteristics.vref;
            last_adc_vref = adc_vref;
            last_atten = self->atten;
            last_adc_width = self->width;
            last_adc_num = self->adc_num;
        }
        if ((self->adc_num == ADC_UNIT_2) && (last_atten2 != self->atten)) {
            adc2_config_channel_atten(self->adc_chan, self->atten);
            last_atten2 = self->atten;
        }
        esp_err_t err = esp_adc_cal_get_voltage(self->adc_chan, &characteristics, (uint32_t *)&adc_val);
        if (err != ESP_OK) {
            if (self->adc_num == ADC_UNIT_2) mp_raise_ValueError("Cannot read, ADC2 used by Wi-Fi");
            else mp_raise_ValueError("Error reading");
        }
    }
    return MP_OBJ_NEW_SMALL_INT(adc_val);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_read_obj, madc_read);

//---------------------------------------------------------------
STATIC mp_obj_t madc_atten(mp_obj_t self_in, mp_obj_t atten_in) {
    madc_obj_t *self = self_in;
    _is_init(self, true, true);

    if (self->gpio_id == GPIO_NUM_MAX) return mp_const_none;

    adc_atten_t atten = mp_obj_get_int(atten_in);
    if ((atten < ADC_ATTEN_DB_0) || (atten > ADC_ATTEN_DB_11)) mp_raise_ValueError("Unsupported atten value");

    esp_err_t err;
    if (self->adc_num == ADC_UNIT_1) {
        err = adc1_config_channel_atten(self->adc_chan, atten);
        if (err != ESP_OK) mp_raise_ValueError("Parameter Error (config attenuation)");
    }
    else {
        if (last_atten2 != atten) {
            adc2_config_channel_atten(self->adc_chan, atten);
            last_atten2 = self->atten;
        }
    }
    self->atten = atten;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(madc_atten_obj, madc_atten);

//---------------------------------------------------------------
STATIC mp_obj_t madc_width(mp_obj_t self_in, mp_obj_t width_in) {
    madc_obj_t *self = self_in;
    _is_init(self, true, true);

    if (self->gpio_id == GPIO_NUM_MAX) return mp_const_none;

    adc_bits_width_t width = mp_obj_get_int(width_in);
    if ((width < ADC_WIDTH_9Bit) || (width > ADC_WIDTH_12Bit)) mp_raise_ValueError("Unsupported width value");

    if (self) self->width = width;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(madc_width_obj, madc_width);


//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t madc_vref_togpio(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_vref,			MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
            { MP_QSTR_vref_topin,	MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[0].u_int > 0) {
        uint32_t vref = args[0].u_int;
        if ((vref < 1000) || (vref > 1200)) mp_raise_ValueError("Vref range: 1000~1200 (mV)");
        adc_vref = vref;
    }
    uint32_t gpio = 0;
    if (args[1].u_int > 0) {
        gpio = args[1].u_int;
        if ((gpio < 25) || (gpio > 27)) mp_raise_ValueError("Only gpios 25,26,27 can be used");

        esp_err_t status = adc2_vref_to_gpio(gpio);
        if (status != ESP_OK) mp_raise_ValueError("Error routing Vref to gpio");

    }

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(adc_vref);
    if (gpio > 0) tuple[1] = mp_obj_new_int(gpio);
    else tuple[1] = mp_const_false;
    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(madc_vref_togpio_obj, 0, madc_vref_togpio);


// ==== Collect and i2s read functions ======================================================

//-------------------------------------------------------------------------------------------
STATIC mp_obj_t madc_collect(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_freq, ARG_len, ARG_readmv, ARG_data, ARG_callback, ARG_wait };
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_freq,     MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_len,      MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 0} },
            { MP_QSTR_readmv,   MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false}},
            { MP_QSTR_data,     MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_callback, MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_wait,     MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false}},
    };

    madc_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    _is_init(self, true, true);

    // Get arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    adc_timer_active = true;

    if (mpy_timers_used[ADC_TIMER_NUM]) {
        adc_timer_active = false;
        mp_raise_ValueError("ADC timer used by Timer module!");
    }

    if (self->gpio_id == GPIO_NUM_MAX) {
        adc_timer_active = false;
        mp_raise_ValueError("collect for hall sensor not allowed");
    }

    double freq = mp_obj_get_float(args[ARG_freq].u_obj);
    if ((freq < 0.001) || (freq > 18000.0)) {
        mp_raise_ValueError("frequency out of range (0.001 - 18000 Hz)");
    }
    double interv = (1.0 / freq) * ADC_TIMER_FREQ;
    self->callback = NULL;
    self->buffer = NULL;
    self->buf_ptr = 0;
    self->cal_read = args[ARG_readmv].u_bool;
    self->buf_len = args[ARG_len].u_int;
    self->interval = (int64_t)(round(interv));

    if (args[ARG_callback].u_obj != mp_const_none) {
        if ((!MP_OBJ_IS_FUN(args[ARG_callback].u_obj)) && (!MP_OBJ_IS_METH(args[ARG_callback].u_obj))) {
            adc_timer_active = false;
            mp_raise_ValueError("callback function expected");
        }
        self->callback = args[ARG_callback].u_obj;
    }

    if (args[ARG_data].u_obj != mp_const_none) {
        // Collect to the provided array
        if (!MP_OBJ_IS_TYPE(args[ARG_data].u_obj, &mp_type_array)) {
            adc_timer_active = false;
            mp_raise_ValueError("array argument expected");
        }
        mp_obj_array_t * arr = (mp_obj_array_t *)MP_OBJ_TO_PTR(args[ARG_data].u_obj);
        if ((arr->typecode == 'h') || (arr->typecode == 'H')) {
            self->val_shift = 0;
        }
        else if (arr->typecode == 'B') {
            self->val_shift = self->width + 1;
        }
        else {
            adc_timer_active = false;
            mp_raise_ValueError("array argument of type 'h', 'H' or 'B' expected");
        }
        if (arr->len < 1) {
            self->buf_len = 0;
            adc_timer_active = false;
            mp_raise_ValueError("array argument length must be >= 1");
        }
        self->buffer = arr->items;
        if (self->buf_len < 1) self->buf_len = arr->len;
        else if (arr->len < self->buf_len) self->buf_len = arr->len;
    }
    else if (self->buf_len < 1) {
        self->buf_len = 0;
        adc_timer_active = false;
        mp_raise_ValueError("length must be >= 1");
    }

    collect_active = true;
    if (start_adc_timer(self, args[ARG_wait].u_bool) != ESP_OK) {
        adc_timer_active = false;
        collect_active = false;
        mp_raise_ValueError("Error starting ADC timer");
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(madc_collect_obj, 0, madc_collect);

//----------------------------------------------------------------------------------------------
STATIC mp_obj_t madc_read_timed(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_data, ARG_freq, ARG_len, ARG_byte, ARG_wait, ARG_callback };
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_data,     MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_freq,     MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_nsamples, MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = -1} },
            { MP_QSTR_byte,     MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = true} },
            { MP_QSTR_wait,     MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false}},
            { MP_QSTR_callback, MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
    };

    madc_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    _is_init(self, true, true);

    if (self->gpio_id == GPIO_NUM_MAX) {
        mp_raise_ValueError("timed read for hall sensor not allowed");
    }
    if (i2s_driver_installed) {
        mp_raise_ValueError("Error: i2s used by other module");
    }

    // Get arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    self->callback = NULL;
    self->buffer = NULL;
    self->buf_ptr = 0;
    self->cal_read = false;
    self->buf_len = 0;
    self->interval = 0;
    self->val_shift = 0;

    int freq = mp_obj_get_int(args[ARG_freq].u_obj);
    if ((freq < 5000) || (freq > 500000)) {
        mp_raise_ValueError("frequency out of range (5000 - 500000 Hz)");
    }

    if (args[ARG_callback].u_obj != mp_const_none) {
        if ((!MP_OBJ_IS_FUN(args[ARG_callback].u_obj)) && (!MP_OBJ_IS_METH(args[ARG_callback].u_obj))) {
            mp_raise_ValueError("callback function expected");
        }
        self->callback = args[ARG_callback].u_obj;
    }

    bool wait = args[ARG_wait].u_bool;

    size_t length = args[ARG_len].u_int;

    if (MP_OBJ_IS_STR(args[ARG_data].u_obj)) {
        // reading to file
        if (length < 1) {
            mp_raise_ValueError("file length must be >= 1");
        }
        const char *dac_file = NULL;
        char fullname[128] = {'\0'};

        dac_file = mp_obj_str_get_str(args[0].u_obj);
        int res = physicalPath(dac_file, fullname);
        if ((res != 0) || (strlen(fullname) == 0)) {
            mp_raise_ValueError("Error resolving file name");
        }
        self->fhndl = fopen(fullname, "wb");
        if (self->fhndl == NULL) {
            mp_raise_ValueError("Error opening file");
        }
        // Allocate temporary data buffer
        if (args[ARG_byte].u_bool) {
            self->val_shift = self->width + 1;
        }
        self->buf_len = length;
    }
    else if (MP_OBJ_IS_TYPE(args[ARG_data].u_obj, &mp_type_array)) {
        // reading to array
        mp_buffer_info_t src;
        mp_get_buffer_raise(args[ARG_data].u_obj, &src, MP_BUFFER_WRITE);

        mp_obj_array_t * arr = (mp_obj_array_t *)MP_OBJ_TO_PTR(args[ARG_data].u_obj);
        if ((arr->typecode == 'h') && (arr->typecode != 'H') && (arr->typecode != 'B')) {
            mp_raise_ValueError("array argument of type 'h', 'H' or 'B' expected");
        }
        if (arr->typecode == 'B') {
            self->val_shift = self->width + 1;
        }
        self->buffer = arr->items;

        if (arr->len < 1) {
            mp_raise_ValueError("array argument length must be >= 1");
        }
        if ((length > 0) && (length < arr->len)) self->buf_len = length;
        else self->buf_len = arr->len;
    }
    else {
        mp_raise_ValueError("array or file name argument expected");
    }

    // configure i2s
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN, // Only RX, ADC input
        //.mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN | I2S_MODE_ADC_BUILT_IN,
        .sample_rate = freq,
        .bits_per_sample = 16,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .dma_buf_count = 2,
        .dma_buf_len = 1024,
        .use_apll = false,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .fixed_mclk = 0
    };

    // install and start i2s driver
    i2s_driver_install(0, &i2s_config, 0, NULL);
    i2s_driver_installed = true;
    // init ADC pad
    i2s_set_adc_mode(self->adc_num, self->adc_chan);
    i2s_adc_enable(0);
    //i2s_set_sample_rates(0, freq);

    task_stop = false;
    esp_log_level_set("I2S", ESP_LOG_ERROR);
    #if CONFIG_MICROPY_USE_BOTH_CORES
    xTaskCreate(adc_task, "ADC_task", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, NULL);
    #else
    xTaskCreatePinnedToCore(adc_task, "ADC_task", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, NULL, MainTaskCore);
    #endif

    if (wait) {
        mp_hal_delay_ms(3);
        while (task_running) {
            mp_hal_delay_ms(3);
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(madc_read_timed_obj, 0, madc_read_timed);

//----------------------------------------------------
STATIC mp_obj_t madc_get_collected(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
    _is_init(self, true, true);

    if (self->buf_len == 0) {
        mp_raise_ValueError("no data collected");
    }

    // return statistics
    int rms = self->rms_summ / self->buf_len;
    int avrg = self->summ / self->buf_len;
    double drms = sqrt((double)(rms));
    rms = (int)(round(drms));
    mp_obj_t tuple[4];
    tuple[0] = mp_obj_new_int(self->min);
    tuple[1] = mp_obj_new_int(self->max);
    tuple[2] = mp_obj_new_int(avrg);
    tuple[3] = mp_obj_new_int(rms);

    return mp_obj_new_tuple(4, tuple);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_get_collected_obj, madc_get_collected);

//-----------------------------------------------
STATIC mp_obj_t madc_progress(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
    _is_init(self, true, false);

    bool active = collect_active | task_running;
    mp_obj_t tuple[4];
    tuple[0] = mp_obj_new_bool(active);
    tuple[1] = mp_obj_new_int(self->buf_ptr);
    tuple[2] = mp_obj_new_int(self->buf_len);
    if (active) tuple[3] = mp_obj_new_int_from_ull(esp_timer_get_time() /*mp_hal_ticks_us()*/ - collect_start_time);
    else tuple[3] = mp_obj_new_int_from_ull(collect_end_time - collect_start_time);

    return mp_obj_new_tuple(4, tuple);
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_progress_obj, madc_progress);

//---------------------------------------------------
STATIC mp_obj_t madc_stop_collect(mp_obj_t self_in) {
    madc_obj_t *self = self_in;
    _is_init(self, true, false);

    if (task_running) {
        task_stop = true;
        while (task_running) {
            vTaskDelay(2);
        }
    }
    if (collect_active) {
        timer_disable_intr((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        timer_pause((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        if (adc_timer_handle) {
            esp_intr_free(adc_timer_handle);
            adc_timer_handle = NULL;
        }
        collect_end_time = esp_timer_get_time(); //mp_hal_ticks_us();
        self->buffer = NULL;
        adc_timer_active = false;
        collect_active = false;
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(madc_stop_collect_obj, madc_stop_collect);


//=========================================================
STATIC const mp_rom_map_elem_t madc_locals_dict_table[] = {
        { MP_ROM_QSTR(MP_QSTR_read),		MP_ROM_PTR(&madc_read_obj) },
        { MP_ROM_QSTR(MP_QSTR_readraw),		MP_ROM_PTR(&madc_readraw_obj) },
        { MP_ROM_QSTR(MP_QSTR_read_timed),  MP_ROM_PTR(&madc_read_timed_obj) },
        { MP_ROM_QSTR(MP_QSTR_collect),		MP_ROM_PTR(&madc_collect_obj) },
        { MP_ROM_QSTR(MP_QSTR_collected),	MP_ROM_PTR(&madc_get_collected_obj) },
        { MP_ROM_QSTR(MP_QSTR_stopcollect), MP_ROM_PTR(&madc_stop_collect_obj) },
        { MP_ROM_QSTR(MP_QSTR_progress),	MP_ROM_PTR(&madc_progress_obj) },
        { MP_ROM_QSTR(MP_QSTR_atten),		MP_ROM_PTR(&madc_atten_obj) },
        { MP_ROM_QSTR(MP_QSTR_width),		MP_ROM_PTR(&madc_width_obj) },
        { MP_ROM_QSTR(MP_QSTR_vref),		MP_ROM_PTR(&madc_vref_togpio_obj) },
        { MP_ROM_QSTR(MP_QSTR_deinit),		MP_ROM_PTR(&madc_deinit_obj) },

        { MP_ROM_QSTR(MP_QSTR_HALL),		MP_ROM_INT(ADC1_CHANNEL_MAX) },

        { MP_ROM_QSTR(MP_QSTR_ATTN_0DB),	MP_ROM_INT(ADC_ATTEN_0db) },
        { MP_ROM_QSTR(MP_QSTR_ATTN_2_5DB),	MP_ROM_INT(ADC_ATTEN_2_5db) },
        { MP_ROM_QSTR(MP_QSTR_ATTN_6DB),	MP_ROM_INT(ADC_ATTEN_6db) },
        { MP_ROM_QSTR(MP_QSTR_ATTN_11DB),	MP_ROM_INT(ADC_ATTEN_11db) },

        { MP_ROM_QSTR(MP_QSTR_WIDTH_9BIT),	MP_ROM_INT(ADC_WIDTH_9Bit) },
        { MP_ROM_QSTR(MP_QSTR_WIDTH_10BIT),	MP_ROM_INT(ADC_WIDTH_10Bit) },
        { MP_ROM_QSTR(MP_QSTR_WIDTH_11BIT),	MP_ROM_INT(ADC_WIDTH_11Bit) },
        { MP_ROM_QSTR(MP_QSTR_WIDTH_12BIT),	MP_ROM_INT(ADC_WIDTH_12Bit) },
};

STATIC MP_DEFINE_CONST_DICT(madc_locals_dict, madc_locals_dict_table);

//======================================
const mp_obj_type_t machine_adc_type = {
        { &mp_type_type },
        .name = MP_QSTR_ADC,
        .print = madc_print,
        .make_new = madc_make_new,
        .locals_dict = (mp_obj_t)&madc_locals_dict,
};
