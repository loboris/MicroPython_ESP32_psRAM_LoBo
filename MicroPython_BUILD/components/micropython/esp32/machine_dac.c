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
#include <sys/stat.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"

#include "driver/gpio.h"
#include "driver/dac.h"
#include "driver/timer.h"
#include "soc/rtc_io_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "modmachine.h"
#include "py/objarray.h"
#include "extmod/vfs_native.h"


typedef struct _mdac_obj_t {
    mp_obj_base_t base;
    int             gpio_id;
    dac_channel_t   dac_id;
    uint8_t         *buffer;
    size_t          buf_len;
    size_t          buf_ptr;
    FILE            *fhndl;
    uint64_t        timer_interval;
    uint8_t         dac_timer_mode;
} mdac_obj_t;

extern int MainTaskCore;

static bool trepeat = false;
static bool task_running = false;
static bool task_stop = false;
static bool timer_stop = false;
static bool cosine_enabled = false;
static bool dac_i2s_driver_installed = false;
static bool dac_timer_active = false;
static intr_handle_t dac_timer_handle = NULL;

// === ESP32 cosine generator functions ===

//------------------------------
static void dac_cosine_disable()
{
    // Disable tone generator common to both channels
    CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_SW_TONE_EN);

    // Disable / disconnect tone tone generator
    CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN1_M);
    CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN2_M);
    // Invert MSB, otherwise part of waveform will have inverted
    SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_INV1, 0, SENS_DAC_INV1_S);
    SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_INV2, 0, SENS_DAC_INV2_S);

    cosine_enabled = false;
}

//--------------------------------------------------
static void dac_cosine_enable(dac_channel_t channel)
{
    dac_cosine_disable();
    // Enable tone generator common to both channels
    SET_PERI_REG_MASK(SENS_SAR_DAC_CTRL1_REG, SENS_SW_TONE_EN);
    switch(channel) {
        case DAC_CHANNEL_1:
            // Enable / connect tone tone generator on / to this channel
            SET_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN1_M);
            // Invert MSB, otherwise part of waveform will have inverted
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_INV1, 2, SENS_DAC_INV1_S);
            break;
        case DAC_CHANNEL_2:
            SET_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN2_M);
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_INV2, 2, SENS_DAC_INV2_S);
            break;
        default:
            break;
    }
    cosine_enabled = true;
}

/*
 * Set frequency of internal CW generator common to both DAC channels
 *
 * clk_8m_div: 0b000 - 0b111
 * frequency_step: range 0x0001 - 0xFFFF
 *
 */
//---------------------------------------------------------------
static void dac_frequency_set(int clk_8m_div, int frequency_step)
{
    REG_SET_FIELD(RTC_CNTL_CLK_CONF_REG, RTC_CNTL_CK8M_DIV_SEL, clk_8m_div);
    SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL1_REG, SENS_SW_FSTEP, frequency_step, SENS_SW_FSTEP_S);
}


/*
 * Scale output of a DAC channel using two bit pattern:
 *
 * - 00: no scale
 * - 01: scale to 1/2
 * - 10: scale to 1/4
 * - 11: scale to 1/8
 *
 */
//---------------------------------------------------------
static void dac_scale_set(dac_channel_t channel, int scale)
{
    switch(channel) {
        case DAC_CHANNEL_1:
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_SCALE1, scale, SENS_DAC_SCALE1_S);
            break;
        case DAC_CHANNEL_2:
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_SCALE2, scale, SENS_DAC_SCALE2_S);
            break;
        default :
           printf("Channel %d\n", channel);
    }
}


/*
 * Offset output of a DAC channel
 *
 * Range 0x00 - 0xFF
 *
 */
//-----------------------------------------------------------
static void dac_offset_set(dac_channel_t channel, int offset)
{
    switch(channel) {
        case DAC_CHANNEL_1:
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_DC1, offset, SENS_DAC_DC1_S);
            break;
        case DAC_CHANNEL_2:
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_DC2, offset, SENS_DAC_DC2_S);
            break;
        default :
           printf("Channel %d\n", channel);
    }
}


/*
 * Invert output pattern of a DAC channel
 *
 * - 00: does not invert any bits,
 * - 01: inverts all bits,
 * - 10: inverts MSB,
 * - 11: inverts all bits except for MSB
 *
 */
//-----------------------------------------------------------
static void dac_invert_set(dac_channel_t channel, int invert)
{
    switch(channel) {
        case DAC_CHANNEL_1:
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_INV1, invert, SENS_DAC_INV1_S);
            break;
        case DAC_CHANNEL_2:
            SET_PERI_REG_BITS(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_INV2, invert, SENS_DAC_INV2_S);
            break;
        default :
           printf("Channel %d\n", channel);
    }
}

// === DAC functions ===

//======================================
static void dac_task(void *pvParameters)
{
    task_running = true;
    mdac_obj_t *self = (mdac_obj_t *)pvParameters;

    int buf_idx = 0;
    int write_size = self->buf_len;
    if (write_size > 4096) write_size = 4096;

    size_t i2s_bytes_write;
    while (true) {
        if (task_stop) break;
        if (buf_idx >= self->buf_len) break;
        // write the buffer/file
        while (buf_idx < self->buf_len) {
            if (task_stop) break;
            if (self->fhndl) {
                // from file
                write_size = fread(self->buffer, 1, write_size, self->fhndl);
                if (write_size > 0) {
                    i2s_write(0, self->buffer, write_size, &i2s_bytes_write, 1000);
                    if (i2s_bytes_write != write_size) {
                        ESP_LOGE("DAC", "I2S error writing");
                        task_stop = true;
                        break;
                    }
                }
                else {
                    ESP_LOGE("DAC", "error reading from file");
                    task_stop = true;
                    break;
                }
            }
            else {
                // from buffer
                i2s_write(0, self->buffer + buf_idx, write_size, &i2s_bytes_write, 1000);
                if (i2s_bytes_write != write_size) {
                    ESP_LOGE("DAC", "I2S error writing");
                    task_stop = true;
                    break;
                }
            }
            buf_idx += write_size;
            if (buf_idx >= self->buf_len) break;
            write_size = self->buf_len - buf_idx;
            if (write_size > 4096) write_size = 4096;

            //#if CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU0 || CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1
            //vTaskDelay(0); // allow other core idle task to reset the watchdog
            //#endif
        }
        if (trepeat == 0) break;
        // Repeat writing from start of file/buffer
        buf_idx = 0;
        write_size = self->buf_len;
        if (write_size > 4096) write_size = 4096;
        if (self->fhndl) {
            if (fseek(self->fhndl, 0, SEEK_SET) != 0) break;
        }
    }

    if (self->fhndl) {
        fclose(self->fhndl);
        free(self->buffer);
    }
    i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
    i2s_stop(0);
    i2s_driver_uninstall(0);
    dac_i2s_driver_installed = false;
    i2s_driver_installed = false;
    dac_i2s_disable();
    dac_output_enable(self->dac_id);
    dac_output_voltage(self->dac_id, 128);

    esp_log_level_set("I2S", CONFIG_LOG_DEFAULT_LEVEL);
    task_stop = false;
    task_running = false;

    vTaskDelete(NULL);
}

//--------------------------------------
STATIC void dac_timer_isr(void *self_in)
{
    mdac_obj_t *self = (mdac_obj_t *)self_in;

    // Clear timer interrupt
    if (ADC_TIMER_NUM & 2) {
        if (ADC_TIMER_NUM & 1) TIMERG1.int_clr_timers.t1 = 1;
        else TIMERG1.int_clr_timers.t0 = 1;
    }
    else {
        if (ADC_TIMER_NUM & 1) TIMERG0.int_clr_timers.t1 = 1;
        else TIMERG0.int_clr_timers.t0 = 1;
    }

    if (self->dac_timer_mode == 1) {
        // Generate random noise
        uint32_t rnd = esp_random();
        rnd = ((rnd & 0xff) ^ ((rnd >> 8) & 0xff) ^ ((rnd >> 16) & 0xff) ^ ((rnd >> 24) & 0xff));
        //rnd = ((rnd & 0xff) + ((rnd >> 8) & 0xff) + ((rnd >> 16) & 0xff) + ((rnd >> 24) & 0xff));
        dac_output_voltage(self->dac_id, (uint8_t)rnd);
    }
    else if (self->dac_timer_mode == 2) {
        // Output DAC values from buffer
        dac_output_voltage(self->dac_id, self->buffer[self->buf_ptr]);
        self->buf_ptr++;
        if (self->buf_ptr >= self->buf_len) {
            if (trepeat) self->buf_ptr = 0;
            else timer_stop = true;
        }
    }
    else timer_stop = true;

    if (timer_stop) {
        // --- Finished, all data read or ADC read error ---
        timer_disable_intr((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        timer_pause((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        if (dac_timer_handle) {
            esp_intr_free(dac_timer_handle);
            dac_timer_handle = NULL;
        }
        adc_timer_active = false;
        dac_timer_active = false;
        timer_stop = false;
    }
    else {
        // --- Not yet finished, enable alarm interrupt ---
        if (ADC_TIMER_NUM & 2) TIMERG1.hw_timer[ADC_TIMER_NUM & 1].config.alarm_en = true;
        else TIMERG0.hw_timer[ADC_TIMER_NUM & 1].config.alarm_en = true;
    }
}

//------------------------------------------------
STATIC esp_err_t start_dac_timer(mdac_obj_t *self)
{
    if (dac_timer_handle) {
        esp_intr_free(dac_timer_handle);
        dac_timer_handle = NULL;
    }

    timer_config_t config;
    config.counter_dir = TIMER_COUNT_UP;
    config.intr_type = TIMER_INTR_LEVEL;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.auto_reload = true;
    config.divider = ADC_TIMER_DIVIDER; // 1 MHz

    esp_err_t err = timer_init((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, &config);
    if (err != ESP_OK) return -1;

    // Timer's counter will initially start from value below.
    // Also, if auto_reload is set, this value will be automatically reload on alarm

    err = timer_set_counter_value((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, 0x00000000ULL);
    if (err != ESP_OK) return -2;

    // Configure the alarm value and the interrupt on alarm.
    err = timer_set_alarm_value((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, self->timer_interval);
    if (err != ESP_OK) return -3;

    // Enable timer interrupt
    err = timer_enable_intr((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
    if (err != ESP_OK) return -4;

    // Register interrupt callback
    err = timer_isr_register((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1, dac_timer_isr, (void*)self, 0, &dac_timer_handle);
    if (err != ESP_OK) return -5;

    // Start the timer
    err = timer_start((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
    if (err != ESP_OK) {
        timer_pause((ADC_TIMER_NUM >> 1) & 1, ADC_TIMER_NUM & 1);
        if (dac_timer_handle) {
            esp_intr_free(dac_timer_handle);
            dac_timer_handle = NULL;
        }
        return -6;
    }
    return ESP_OK;
}


//-----------------------------------------
static void dac_func_stop(mdac_obj_t *self)
{
    if (dac_timer_active) {
        timer_stop = true;
        while (dac_timer_active) {
            vTaskDelay(2);
        }
    }
    if (cosine_enabled) dac_cosine_disable();
    if (task_running) {
        task_stop = true;
        while (task_running) {
            vTaskDelay(2);
        }
    }
    if (dac_i2s_driver_installed) {
        i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
        i2s_stop(0);
        i2s_driver_uninstall(0);
        dac_i2s_driver_installed = false;
        i2s_driver_installed = false;
        dac_i2s_disable();
        dac_output_enable(self->dac_id);
        dac_output_voltage(self->dac_id, 128);
    }
}

//------------------------------------------------------------
static void _is_init(mdac_obj_t *self, bool iflag, bool cflag)
{
    if ((iflag) && (self->gpio_id < 0)) {
        mp_raise_ValueError("Not initialized");
    }
    if ((cflag) && ((task_running) || (dac_i2s_driver_installed) || (cosine_enabled) || (dac_timer_active))) {
        mp_raise_ValueError("timed write or waveform in progress");
    }
}

//------------------------------------------
static bool gen_waveform(int type, int freq)
{
    if ((type < 1) || (type > 3)) return false;

    size_t i2s_bytes_write;
    int j = 0;
    int buflen = 1024;
    int nsamples = 256;

    if (type == 1) {
        nsamples *= 2;
        buflen *= 2;
    }
    else freq = freq / 2;

    uint8_t *samples_data = malloc(buflen);
    if (samples_data == NULL) return false;

    if (type == 1) {
        // triangle wave
        for (int i = 0; i<nsamples; i++) {
            if ((i & 0x100) == 0) {
                samples_data[j++] = 0;
                samples_data[j++] = i & 0xFF;
                samples_data[j++] = 0;
                samples_data[j++] = i & 0xFF;
            }
            else {
                samples_data[j++] = 0;
                samples_data[j++] = 255 - (i & 0xFF);
                samples_data[j++] = 0;
                samples_data[j++] = 255 - (i & 0xFF);
            }
        }
    }
    else if (type == 2) {
        // ramp wave
        for (int i = 0; i<nsamples; i++) {
            samples_data[j++] = 0;
            samples_data[j++] = i & 0xFF;
            samples_data[j++] = 0;
            samples_data[j++] = i & 0xFF;
        }
    }
    else if (type == 3) {
        // tooth saw wave
        for (int i = 0; i<nsamples; i++) {
            samples_data[j++] = 0;
            samples_data[j++] = 255 - (i & 0xFF);
            samples_data[j++] = 0;
            samples_data[j++] = 255 - (i & 0xFF);
        }
    }

    i2s_set_clk(0, freq*64, 16, I2S_CHANNEL_MONO);
    //ToDo: check why multiple writes are needed?!
    for (int i=0; i<32; i++) {
        i2s_write(0, samples_data, buflen, &i2s_bytes_write, 100);
    }
    free(samples_data);
    return true;
}


// === MicroPython DAC bindings ===

//----------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mdac_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {

    mp_arg_check_num(n_args, n_kw, 1, 1, true);

    gpio_num_t pin_id = machine_pin_get_gpio(args[0]);

    if ((pin_id != GPIO_NUM_25) && (pin_id != GPIO_NUM_26)) {
        mp_raise_ValueError("invalid Pin for DAC");
    }

    mdac_obj_t *self = m_new_obj(mdac_obj_t);
    self->base.type = &machine_dac_type;
    self->gpio_id = pin_id;
    if (pin_id == 25) self->dac_id = DAC_CHANNEL_1;
    else self->dac_id = DAC_CHANNEL_2;
    self->buffer = NULL;
    self->buf_len = 0;
    self->fhndl = NULL;

    dac_i2s_disable();
    esp_err_t err = dac_output_enable(self->dac_id);
    if (err == ESP_OK) {
        err = dac_output_voltage(self->dac_id, 0);
    }
    if (err != ESP_OK) mp_raise_ValueError("DAC Parameter Error");
    return MP_OBJ_FROM_PTR(self);
}

//---------------------------------------------------------------------------------------
STATIC void mdac_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mdac_obj_t *self = self_in;

    if (self->gpio_id < 0) {
        mp_printf(print, "DAC( deinitialized )");
        return;
    }
    mp_printf(print, "DAC(Pin(%u), channel: %d)", self->gpio_id, self->dac_id);
}

//---------------------------------------------------------------
STATIC mp_obj_t mdac_write(mp_obj_t self_in, mp_obj_t value_in) {
    mdac_obj_t *self = self_in;
    _is_init(self, true, true);

    int value = mp_obj_get_int(value_in);
    if (value < 0 || value > 255) mp_raise_ValueError("Value out of range");

    esp_err_t err = dac_output_voltage(self->dac_id, value);
    if (err != ESP_OK) mp_raise_ValueError("Parameter Error");
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mdac_write_obj, mdac_write);


//--------------------------------------------------------------------------------------------
STATIC mp_obj_t mdac_waveform(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_freq, ARG_type, ARG_duration, ARG_scale, ARG_offset, ARG_invert, ARG_len };
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_freq,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 1000} },
            { MP_QSTR_type,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
            { MP_QSTR_duration, MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0}},
            { MP_QSTR_scale,    MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0}},
            { MP_QSTR_offset,   MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0}},
            { MP_QSTR_invert,   MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 2}},
    };

    mdac_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    _is_init(self, true, false);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    dac_func_stop(self);

    int type = args[ARG_type].u_int;
    int freq = args[ARG_freq].u_int;
    if (type < 0 || type > 4) mp_raise_ValueError("Unknown function type");

    if (type == 0) {
        // --- sine generator using ESP32 hw cosine generator ---
        // freq = 8000000 x frequency_step / 65536
        // frequency_step = (freq * 65536) / 8000000
        if (freq < 16 || freq > 32000) mp_raise_ValueError("Frequency out of range (16-32000 Hz)");
        uint64_t fs = freq * 65536;
        int fdiv = 1;
        if (freq < 256) fdiv = 4;
        if (freq < 64) fdiv = 8;
        fs /= (8000000 / fdiv);
        dac_frequency_set((fdiv-1), (int)fs);
        dac_scale_set(self->dac_id, args[ARG_scale].u_int & 3);
        dac_offset_set(self->dac_id, (int8_t)args[ARG_offset].u_int);
        dac_invert_set(self->dac_id, args[ARG_invert].u_int & 3);
        dac_cosine_enable(self->dac_id);

        goto exit;
    }

    if (type == 4) {
        // Noise
        if ((mpy_timers_used[ADC_TIMER_NUM]) || (adc_timer_active)) {
            mp_raise_ValueError("DAC timer used by other module!");
        }
        adc_timer_active = true;
        dac_timer_active = true;
        self->dac_timer_mode = 1;
        timer_stop = false;
        if (freq < 500 || freq > 32000) mp_raise_ValueError("Frequency out of range (500-32000 Hz)");
        self->timer_interval = (int)ADC_TIMER_FREQ / freq;
        int err = start_dac_timer(self) != ESP_OK;
        if (err) {
            ESP_LOGE("DAC", "Error starting DAC timer (%d)", err);
        }
        goto exit;
    }

    // --- For other waveforms we use I2S peripheral to generate the waveform ---
    if ((!dac_i2s_driver_installed) && (i2s_driver_installed)) {
        mp_raise_ValueError("Error: i2s used by other module");
    }

    if ((type == 1) && (freq < 170 || freq > 3600)) mp_raise_ValueError("Frequency out of range (170 - 3600 Hz)");
    else if ((freq < 170 || freq > 7200)) mp_raise_ValueError("Frequency out of range (170 - 7200 Hz)");

    i2s_config_t i2s_config = {
        //.mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN, // Only TX, DAC output
        .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN | I2S_MODE_ADC_BUILT_IN,
        .sample_rate = freq * 64,
        .bits_per_sample = 16,
        .channel_format = (self->dac_id == DAC_CHANNEL_1) ? I2S_CHANNEL_FMT_ALL_RIGHT : I2S_CHANNEL_FMT_ALL_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .dma_buf_count = 2,
        .dma_buf_len = 256,
        .use_apll = true,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, //Interrupt level 1
        .fixed_mclk = 0
    };
    //install and start i2s driver
    i2s_driver_install(0, &i2s_config, 0, NULL);
    i2s_driver_installed = true;
    dac_i2s_driver_installed = true;
    //init DAC pad
    i2s_set_dac_mode((self->dac_id == DAC_CHANNEL_1) ? I2S_DAC_CHANNEL_RIGHT_EN : I2S_DAC_CHANNEL_LEFT_EN);
    //i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);

    // start the wave
    if (!gen_waveform(type, freq)) {
        i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
        i2s_stop(0);
        i2s_driver_uninstall(0);
        dac_i2s_driver_installed = false;
        i2s_driver_installed = false;
        dac_i2s_disable();
        dac_output_enable(self->dac_id);
        dac_output_voltage(self->dac_id, 128);
        mp_raise_ValueError("Error allocating wave buffer");
    }

exit:
    if (args[ARG_duration].u_int > 0) {
        mp_hal_delay_ms(args[ARG_duration].u_int);
        dac_func_stop(self);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdac_waveform_obj, 0, mdac_waveform);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t mdac_write_buffer(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_data, ARG_freq, ARG_mode, ARG_wait };
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_freq, MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_mode, MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false}},
            { MP_QSTR_wait, MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false}},
    };

    mdac_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    _is_init(self, true, false);

    dac_func_stop(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    double freq = mp_obj_get_float(args[ARG_freq].u_obj);
    if ((freq < 0.001) || (freq > 18000.0)) {
        mp_raise_ValueError("frequency out of range (0.001 - 18000 Hz)");
    }
    double interv = (1.0 / freq) * ADC_TIMER_FREQ;

    // Get arguments
    bool wait = false;
    trepeat = args[ARG_mode].u_bool;
    // only wait if if not continuous mode
    if (!trepeat) wait = args[ARG_wait].u_bool;

    adc_timer_active = true;
    dac_timer_active = true;
    self->buffer = NULL;
    self->buf_len = 0;
    self->buf_ptr = 0;

    if (args[ARG_data].u_obj != mp_const_none) {
        // Play from the provided array
        if (!MP_OBJ_IS_TYPE(args[ARG_data].u_obj, &mp_type_array)) {
            adc_timer_active = false;
            dac_timer_active = false;
            mp_raise_ValueError("array argument expected");
        }
        mp_obj_array_t * arr = (mp_obj_array_t *)MP_OBJ_TO_PTR(args[ARG_data].u_obj);
        if (arr->typecode != 'B') {
            adc_timer_active = false;
            dac_timer_active = false;
            mp_raise_ValueError("array argument of type 'B' expected");
        }
        if (arr->len < 1) {
            self->buf_len = 0;
            adc_timer_active = false;
            dac_timer_active = false;
            mp_raise_ValueError("array argument length must be >= 1");
        }
        self->buffer = arr->items;
        if (self->buf_len < 1) self->buf_len = arr->len;
        else if (arr->len < self->buf_len) self->buf_len = arr->len;
    }
    else {
        adc_timer_active = false;
        dac_timer_active = false;
        mp_raise_ValueError("array argument expected");
    }

    self->dac_timer_mode = 2;
    self->timer_interval = (int64_t)(round(interv));
    int err = start_dac_timer(self) != ESP_OK;
    if (err) {
        adc_timer_active = false;
        dac_timer_active = false;
        self->buffer = NULL;
        self->buf_len = 0;
        self->buf_ptr = 0;
        ESP_LOGE("DAC", "Error starting DAC timer (%d)", err);
    }

    if (wait) {
        mp_hal_delay_ms(3);
        while (dac_timer_active) {
            mp_hal_delay_ms(3);
        }
    }

    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdac_write_buffer_obj, 0, mdac_write_buffer);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mdac_write_timed(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_data, ARG_freq, ARG_mode, ARG_wait };
    const mp_arg_t allowed_args[] = {
            { MP_QSTR_data,       MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
            { MP_QSTR_samplerate, MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 1000} },
            { MP_QSTR_mode,       MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false}},
            { MP_QSTR_wait,       MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false}},
    };

    mdac_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    _is_init(self, true, false);
    if ((!dac_i2s_driver_installed) && (i2s_driver_installed)) {
        mp_raise_ValueError("Error: i2s used by other module");
    }

    dac_func_stop(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int freq = args[ARG_freq].u_int;
    if ((freq < 5000) || (freq > 500000)) {
        mp_raise_ValueError("sample rate out of range (5000 - 500000 Hz)");
    }

    // Get arguments
    bool wait = false;
    trepeat = args[ARG_mode].u_bool;
    // only wait if if not continuous mode
    if (!trepeat) wait = args[ARG_wait].u_bool;

    mp_buffer_info_t src;
    self->fhndl = NULL;
    self->timer_interval = 0;

    if (MP_OBJ_IS_STR(args[ARG_data].u_obj)) {
        const char *dac_file = NULL;
        char fullname[128] = {'\0'};

        dac_file = mp_obj_str_get_str(args[0].u_obj);
        int res = physicalPath(dac_file, fullname);
        if ((res != 0) || (strlen(fullname) == 0)) {
            mp_raise_ValueError("Error resolving file name");
        }
        self->buffer = NULL;
        struct stat sb;
        if (stat(fullname, &sb) != 0) {
            mp_raise_ValueError("Error opening file");
        }
        self->fhndl = fopen(fullname, "rb");
        if (self->fhndl == NULL) {
            mp_raise_ValueError("Error opening file");
        }
        self->buffer = malloc(4096);
        if (self->buffer == NULL) {
            fclose(self->fhndl);
            mp_raise_ValueError("Error allocating dac buffer");
        }
        self->buf_len = sb.st_size;
        wait = false;
    }
    else {
        mp_get_buffer_raise(args[ARG_data].u_obj, &src, MP_BUFFER_READ);
        self->buffer = (uint8_t *)src.buf;
        self->buf_len = src.len;
    }

    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,          // Only TX
        //.mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN | I2S_MODE_ADC_BUILT_IN,
        .sample_rate = freq/2,
        .bits_per_sample = 16,
        .channel_format = (self->dac_id == DAC_CHANNEL_1) ? I2S_CHANNEL_FMT_ALL_RIGHT : I2S_CHANNEL_FMT_ALL_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .dma_buf_count = 2,
        .dma_buf_len = 1024,
        .use_apll = false,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,                                //Interrupt level 1
        .fixed_mclk = 0
    };
    //install and start i2s driver
    i2s_driver_install(0, &i2s_config, 0, NULL);
    i2s_driver_installed = true;
    dac_i2s_driver_installed = true;
    //init DAC pad
    i2s_set_dac_mode((self->dac_id == DAC_CHANNEL_1) ? I2S_DAC_CHANNEL_RIGHT_EN : I2S_DAC_CHANNEL_LEFT_EN);
    //i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    //i2s_set_clk(0, freq, 16, I2S_CHANNEL_MONO);
    //i2s_set_sample_rates(0, freq/2);

    task_stop = false;
    esp_log_level_set("I2S", ESP_LOG_ERROR);
    #if CONFIG_MICROPY_USE_BOTH_CORES
    xTaskCreate(dac_task, "DAC_task", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, NULL);
    #else
    xTaskCreatePinnedToCore(dac_task, "DAC_task", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, NULL, MainTaskCore);
    #endif

    if (wait) {
        mp_hal_delay_ms(3);
        while (task_running) {
            mp_hal_delay_ms(3);
        }
    }

    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdac_write_timed_obj, 0, mdac_write_timed);

//------------------------------------------------------------------
STATIC mp_obj_t mdac_play_wav(size_t n_args, const mp_obj_t *args) {

    mdac_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    _is_init(self, true, false);
    if ((!dac_i2s_driver_installed) && (i2s_driver_installed)) {
        mp_raise_ValueError("Error: i2s used by other module");
    }

    dac_func_stop(self);

    self->buffer = NULL;
    self->buf_len = 0;
    self->fhndl = NULL;
    int freq = 0;
    float fdiv = 0;

    if (n_args == 3) {
        fdiv = mp_obj_get_float(args[2]);
        if ((fdiv < -8.0) || (fdiv > 8.0)) fdiv = 0.0;
    }
    const char *dac_file = NULL;
    char fullname[128] = {'\0'};
    uint8_t hdr[44];

    if (!MP_OBJ_IS_STR(args[1])) {
        mp_raise_ValueError("File name expected");
    }
    dac_file = mp_obj_str_get_str(args[1]);
    int res = physicalPath(dac_file, fullname);
    if ((res != 0) || (strlen(fullname) == 0)) {
        mp_raise_ValueError("Error resolving file name");
    }
    struct stat sb;
    if (stat(fullname, &sb) != 0) {
        mp_raise_ValueError("Error opening file");
    }
    self->buf_len = sb.st_size;
    if (self->buf_len < 45) {
        mp_raise_ValueError("Not a WAV file");
    }

    self->fhndl = fopen(fullname, "rb");
    if (self->fhndl == NULL) {
        mp_raise_ValueError("Error opening file");
    }
    if (fread(hdr, 1, 44, self->fhndl) != 44) {
        fclose(self->fhndl);
        mp_raise_ValueError("Not a WAV file");
    }
    if (((hdr[0] != 'R') || (hdr[1] != 'I') || (hdr[2] != 'F') || (hdr[3] != 'F')) ||
        ((hdr[8] != 'W') || (hdr[9] != 'A') || (hdr[10] != 'V') || (hdr[11] != 'E')) ||
        ((hdr[12] != 'f') || (hdr[13] != 'm') || (hdr[14] != 't') || (hdr[15] != ' ')) ||
        ((hdr[36] != 'd') || (hdr[37] != 'a') || (hdr[38] != 't') || (hdr[39] != 'a')) ) {
        fclose(self->fhndl);
        mp_raise_ValueError("Not a WAV file");
    }
    if (((uint16_t)(hdr[20] | (hdr[21] << 8)) != 1) || ((uint16_t)(hdr[22] | (hdr[23] << 8)) != 1) ||
        ((uint16_t)(hdr[32] | (hdr[33] << 8)) != 1) || ((uint16_t)(hdr[34] | (hdr[35] << 8)) != 8))  {
        fclose(self->fhndl);
        mp_raise_ValueError("Only PCM, 8-bit mono can be played");
    }
    int ffreq = (int)((int)hdr[24] | (int)(hdr[25] << 8) | (int)(hdr[26] << 16) | (int)(hdr[27] << 24));
    freq = ffreq / 4;
    if (fdiv < -0.999) freq = (int)(round((float)freq / (fdiv * -1.0)));
    else if (fdiv > 0.999) freq  = (int)(round((float)freq * fdiv));
    if ((freq < 5000) || (freq > 500000)) {
        fclose(self->fhndl);
        mp_raise_ValueError("invalid sample rate (5000 - 500000 Hz)");
    }

    int data_size = (int)((int)hdr[40] | (int)(hdr[41] << 8) | (int)(hdr[42] << 16) | (int)(hdr[43] << 24));
    if ((data_size + 44) > sb.st_size) {
        fclose(self->fhndl);
        mp_raise_ValueError("invalid file size");
    }
    self->buf_len = data_size;

    self->buffer = malloc(4096);
    if (self->buffer == NULL) {
        fclose(self->fhndl);
        mp_raise_ValueError("Error allocating dac buffer");
    }

    ESP_LOGD("DAC", "Playing WAV, %d Hz, %d bytes", ffreq, data_size);
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,          // Only TX
        //.mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN | I2S_MODE_ADC_BUILT_IN,
        .sample_rate = freq,
        .bits_per_sample = 16,
        .channel_format = (self->dac_id == DAC_CHANNEL_1) ? I2S_CHANNEL_FMT_ALL_RIGHT : I2S_CHANNEL_FMT_ALL_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .dma_buf_count = 2,
        .dma_buf_len = 1024,
        .use_apll = false,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,                                //Interrupt level 1
        .fixed_mclk = 0
    };
    //install and start i2s driver
    i2s_driver_install(0, &i2s_config, 0, NULL);
    i2s_driver_installed = true;
    dac_i2s_driver_installed = true;
    //init DAC pad
    i2s_set_dac_mode((self->dac_id == DAC_CHANNEL_1) ? I2S_DAC_CHANNEL_RIGHT_EN : I2S_DAC_CHANNEL_LEFT_EN);

    task_stop = false;
    esp_log_level_set("I2S", ESP_LOG_ERROR);
    #if CONFIG_MICROPY_USE_BOTH_CORES
    xTaskCreate(dac_task, "DAC_task", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, NULL);
    #else
    xTaskCreatePinnedToCore(dac_task, "DAC_task", 2048, (void *)self, CONFIG_MICROPY_TASK_PRIORITY, NULL, MainTaskCore);
    #endif

    return mp_const_true;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mdac_play_wav_obj, 2, 3, mdac_play_wav);

//-----------------------------------------------
STATIC mp_obj_t mdac_stopfunc(mp_obj_t self_in) {
    mdac_obj_t *self = self_in;

    dac_func_stop(self);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mdac_stopfunc_obj, mdac_stopfunc);

//--------------------------------------------------------------
STATIC mp_obj_t mdac_beep(size_t n_args, const mp_obj_t *args) {
    mdac_obj_t *self = args[0];

    _is_init(self, true, false);

    dac_func_stop(self);

    int freq = mp_obj_get_int(args[1]);
    int duration = mp_obj_get_int(args[2]);
    int scale = 0;
    if (n_args == 4) scale = mp_obj_get_int(args[3]) & 3;
    if (freq < 16 || freq > 32000) mp_raise_ValueError("Frequency out of range (16-32000 Hz)");
    if (duration < 10 || duration > 2000) mp_raise_ValueError("Duration out of range (10-2000 ms)");

    // use cosine generator
    uint64_t fs = freq * 65536;
    int fdiv = 1;
    if (freq < 256) fdiv = 4;
    if (freq < 64) fdiv = 8;
    fs /= (8000000 / fdiv);
    dac_frequency_set((fdiv-1), (int)fs);
    dac_scale_set(self->dac_id, scale);
    dac_cosine_enable(self->dac_id);

    // wait for duration ms
    mp_hal_delay_ms(duration);
    dac_cosine_disable();
    dac_output_voltage(self->dac_id, 128);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mdac_beep_obj, 3, 4, mdac_beep);

//----------------------------------------------------------------
STATIC mp_obj_t mdac_setfreq(mp_obj_t self_in, mp_obj_t freq_in) {
    //mdac_obj_t *self = self_in;

    int freq = mp_obj_get_int(freq_in);
    if (cosine_enabled) {
        if (freq < 130 || freq > 32000) mp_raise_ValueError("Frequency out of range (130-32000 Hz)");
        uint64_t fs = freq * 65536;
        fs /= 8000000;
        dac_frequency_set(0, (int)fs);
    }
    else if (dac_i2s_driver_installed && !task_running) {
        if (freq < 170 || freq > 3600) mp_raise_ValueError("Frequency out of range (170-3600 Hz)");
        i2s_set_clk(0, freq * 64, 16, I2S_CHANNEL_MONO);
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mdac_setfreq_obj, mdac_setfreq);

//-------------------------------------------
STATIC mp_obj_t mdac_deinit(mp_obj_t self_in)
{
    mdac_obj_t *self = self_in;
    _is_init(self, false, true);

    if (self->gpio_id < 0) return mp_const_none;

    dac_cosine_disable();
    dac_output_disable(self->dac_id);
    gpio_pad_select_gpio(self->gpio_id);
    self->gpio_id = -1;

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mdac_deinit_obj, mdac_deinit);


//=========================================================
STATIC const mp_rom_map_elem_t mdac_locals_dict_table[] = {
        { MP_ROM_QSTR(MP_QSTR_write),		MP_ROM_PTR(&mdac_write_obj) },
        { MP_ROM_QSTR(MP_QSTR_write_timed),	MP_ROM_PTR(&mdac_write_timed_obj) },
        { MP_ROM_QSTR(MP_QSTR_write_buffer),MP_ROM_PTR(&mdac_write_buffer_obj) },
        { MP_ROM_QSTR(MP_QSTR_wavplay),     MP_ROM_PTR(&mdac_play_wav_obj) },
        { MP_ROM_QSTR(MP_QSTR_waveform),    MP_ROM_PTR(&mdac_waveform_obj) },
        { MP_ROM_QSTR(MP_QSTR_stopwave),    MP_ROM_PTR(&mdac_stopfunc_obj) },
        { MP_ROM_QSTR(MP_QSTR_deinit),      MP_ROM_PTR(&mdac_deinit_obj) },
        { MP_ROM_QSTR(MP_QSTR_freq),        MP_ROM_PTR(&mdac_setfreq_obj) },
        { MP_ROM_QSTR(MP_QSTR_beep),        MP_ROM_PTR(&mdac_beep_obj) },

        { MP_ROM_QSTR(MP_QSTR_SINE),        MP_ROM_INT(0) },
        { MP_ROM_QSTR(MP_QSTR_TRIANGLE),    MP_ROM_INT(1) },
        { MP_ROM_QSTR(MP_QSTR_RAMP),        MP_ROM_INT(2) },
        { MP_ROM_QSTR(MP_QSTR_SAWTOOTH),    MP_ROM_INT(3) },
        { MP_ROM_QSTR(MP_QSTR_NOISE),       MP_ROM_INT(4) },

        { MP_ROM_QSTR(MP_QSTR_CIRCULAR),    MP_ROM_INT(1) },
        { MP_ROM_QSTR(MP_QSTR_NORMAL),      MP_ROM_INT(1) },
};
STATIC MP_DEFINE_CONST_DICT(mdac_locals_dict, mdac_locals_dict_table);

//======================================
const mp_obj_type_t machine_dac_type = {
        { &mp_type_type },
        .name = MP_QSTR_DAC,
        .print = mdac_print,
        .make_new = mdac_make_new,
        .locals_dict = (mp_obj_t)&mdac_locals_dict,
};
