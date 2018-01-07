/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
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
#include "driver/ledc.h"
#include "esp_err.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "machine_pin.h"
#include "mphalport.h"

// High speed mode
#define PWMODE (LEDC_HIGH_SPEED_MODE)

// Forward dec'l
extern const mp_obj_type_t machine_pwm_type;

typedef struct _esp32_pwm_obj_t {
    mp_obj_base_t base;
    gpio_num_t pin;
    uint8_t active;
    uint8_t channel;
    uint8_t duty;
    ledc_timer_config_t timer_cfg;
} esp32_pwm_obj_t;

// Which channel has which GPIO pin assigned?
// (-1 if not assigned)
STATIC int chan_gpio[LEDC_CHANNEL_MAX/2];

STATIC bool pwm_inited = false;

//----------------------------------------------------
STATIC int set_freq(esp32_pwm_obj_t *self, int newval)
{
    int oval = self->timer_cfg.freq_hz;
    int ores = self->timer_cfg.duty_resolution;
    int dperc = (self->duty * 100) / (2 << self->timer_cfg.duty_resolution);

    // Adjust duty resolution
    int dres = 15;
    while (newval > (80000000 / (1 << dres))) {
    	dres--;
    	if (dres == 0) break;
    }
    if (dres == 0) return 0;

    self->timer_cfg.duty_resolution = dres;
    self->timer_cfg.freq_hz = newval;
    if (ledc_timer_config(&self->timer_cfg) != ESP_OK) {
    	self->timer_cfg.freq_hz = oval;
    	self->timer_cfg.duty_resolution = ores;
        return 0;
    }
    if (dres != ores) {
    	// Adjust
    	int dval = (dperc * (2 << self->timer_cfg.duty_resolution)) / 100;
        ledc_set_duty(PWMODE, self->channel, dval);
        ledc_update_duty(PWMODE, self->channel);
    }
    return 1;
}

/******************************************************************************/

// MicroPython bindings for PWM

//------------------------------------------------------------------------------------------
STATIC void esp32_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "PWM(pin: %u", self->pin);
    if (self->active) {
        int duty = ledc_get_duty(PWMODE, self->channel);
        int dperc = (duty * 100) / (1 << self->timer_cfg.duty_resolution);
        mp_printf(print, ", freq=%u Hz, duty=%u%% [%d], duty resolution=%d bits, channel=%d",
        		ledc_get_freq(PWMODE, self->timer_cfg.timer_num), dperc, duty,
				self->timer_cfg.duty_resolution, self->channel);
    }
    mp_printf(print, ")");
}

//------------------------------------------------------------------------------------------------------------------
STATIC void esp32_pwm_init_helper(esp32_pwm_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_freq, ARG_duty };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty, MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int channel;
    int avail = -1;

    if (self->channel >= LEDC_CHANNEL_MAX/2) {
		// New PWM assignment, find a free PWM channel
		for (channel = 0; channel < LEDC_CHANNEL_MAX/2; ++channel) {
			if (chan_gpio[channel] == -1) {
				avail = channel;
				break;
			}
		}
		// Check if pin already used
		for (channel = 0; channel < LEDC_CHANNEL_MAX/2; ++channel) {
			if (chan_gpio[channel] == self->pin) {
				nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "pin %d already used for pwm", self->pin));
			}
		}
		// Find a free PWM channel
		for (channel = 0; channel < LEDC_CHANNEL_MAX/2; ++channel) {
			if (chan_gpio[channel] == -1) {
				avail = channel;
				break;
			}
		}
		if (avail == -1) {
			mp_raise_ValueError("out of PWM channels");
		}
		self->channel = channel;

		// Setup timer
		self->active = 1;
		if (chan_gpio[channel] == -1) {
			// Init timer
			self->timer_cfg.duty_resolution = LEDC_TIMER_12_BIT;	// 12 bits duty resolution
			self->timer_cfg.freq_hz = 5000;							// 5 kHz frequency
			self->timer_cfg.speed_mode = PWMODE;
			self->timer_cfg.timer_num = channel;
			if (ledc_timer_config(&self->timer_cfg) != ESP_OK) {
				mp_raise_ValueError("Error configuring pwm timer");
			}

			self->duty = 50;
			ledc_channel_config_t cfg = {
				.channel = channel,
				.duty = (1 << self->timer_cfg.duty_resolution) / 2, // 50%
				.gpio_num = self->pin,
				.intr_type = LEDC_INTR_DISABLE,
				.speed_mode = PWMODE,
				.timer_sel = channel,
			};
			if (ledc_channel_config(&cfg) != ESP_OK) {
				nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "PWM not supported on pin %d", self->pin));
			}
			chan_gpio[channel] = self->pin;
		}
    }
    else channel = self->channel;

    // Maybe change PWM timer
    int tval = args[ARG_freq].u_int;
    if (tval != -1) {
        if (tval != self->timer_cfg.freq_hz) {
            if (!set_freq(self, tval)) {
                nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad frequency %d", tval));
            }
        }
    }

    // Set duty cycle?
    int dperc = args[ARG_duty].u_int;
    if (dperc != -1) {
    	if ((dperc < 0) || (dperc > 100)) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad duty %d, use 0 ~ 100 (%%)", dperc));
    	}
    	// Calculate duty value
    	int dval = (dperc * (2 << self->timer_cfg.duty_resolution)) / 100;
        ledc_set_duty(PWMODE, channel, dval);
        ledc_update_duty(PWMODE, channel);
    }
}

//-------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t esp32_pwm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    gpio_num_t pin_id = machine_pin_get_gpio(args[0]);

    // create PWM object from the given pin
    esp32_pwm_obj_t *self = m_new_obj(esp32_pwm_obj_t);
    self->base.type = &machine_pwm_type;
    self->pin = pin_id;
    self->active = 0;
    self->channel = LEDC_CHANNEL_MAX;

    // start the PWM subsystem if it's not already running
    if (!pwm_inited) {
        // Initial condition: no channels assigned
        for (int x = 0; x < LEDC_CHANNEL_MAX/2; ++x) {
            chan_gpio[x] = -1;
        }
        pwm_inited = true;
    }

    // start the PWM running for this channel
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    esp32_pwm_init_helper(self, n_args - 1, args + 1, &kw_args);

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------------------------------------------
STATIC mp_obj_t esp32_pwm_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    esp32_pwm_init_helper(args[0], n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_pwm_init_obj, 1, esp32_pwm_init);

//------------------------------------------------
STATIC mp_obj_t esp32_pwm_deinit(mp_obj_t self_in)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int chan = self->channel;

    // Valid channel?
    if ((chan >= 0) && (chan < LEDC_CHANNEL_MAX/2)) {
        // Mark it unused, and tell the hardware to stop routing
        chan_gpio[chan] = -1;
        ledc_stop(PWMODE, chan, 0);
        self->active = 0;
        self->channel = -1;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pwm_deinit_obj, esp32_pwm_deinit);

//-----------------------------------------------------------------
STATIC mp_obj_t esp32_pwm_freq(size_t n_args, const mp_obj_t *args)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    if (n_args == 1) {
        // get
        return MP_OBJ_NEW_SMALL_INT(self->timer_cfg.freq_hz);
    }

    // set
    int tval = mp_obj_get_int(args[1]);
    if (!set_freq(self, tval)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad frequency %d", tval));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_pwm_freq_obj, 1, 2, esp32_pwm_freq);

//-----------------------------------------------------------------
STATIC mp_obj_t esp32_pwm_duty(size_t n_args, const mp_obj_t *args)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int dperc, duty;

    if (n_args == 1) {
        // get duty
        duty = ledc_get_duty(PWMODE, self->channel);
        dperc = (duty * 100) / (1 << self->timer_cfg.duty_resolution);
        return MP_OBJ_NEW_SMALL_INT(dperc);
    }

    // set
    dperc = mp_obj_get_int(args[1]);
    if (dperc != -1) {
    	if ((dperc < 0) || (dperc > 100)) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad duty %d, use 0 ~ 100 (%%)", dperc));
    	}
    	// Calculate duty value
    	duty = (dperc * (1 << self->timer_cfg.duty_resolution)) / 100;
        ledc_set_duty(PWMODE, self->channel, duty);
        ledc_update_duty(PWMODE, self->channel);
        self->duty = dperc;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_pwm_duty_obj, 1, 2, esp32_pwm_duty);

//-----------------------------------------------
STATIC mp_obj_t esp32_pwm_pause(mp_obj_t self_in)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ledc_timer_pause(PWMODE, self->timer_cfg.timer_num);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pwm_pause_obj, esp32_pwm_pause);

//------------------------------------------------
STATIC mp_obj_t esp32_pwm_resume(mp_obj_t self_in)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ledc_timer_resume(PWMODE, self->timer_cfg.timer_num);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pwm_resume_obj, esp32_pwm_resume);



//==============================================================
STATIC const mp_rom_map_elem_t esp32_pwm_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&esp32_pwm_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&esp32_pwm_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_freq), MP_ROM_PTR(&esp32_pwm_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_duty), MP_ROM_PTR(&esp32_pwm_duty_obj) },
    { MP_ROM_QSTR(MP_QSTR_pause), MP_ROM_PTR(&esp32_pwm_pause_obj) },
    { MP_ROM_QSTR(MP_QSTR_resume), MP_ROM_PTR(&esp32_pwm_resume_obj) },
};
STATIC MP_DEFINE_CONST_DICT(esp32_pwm_locals_dict, esp32_pwm_locals_dict_table);

//======================================
const mp_obj_type_t machine_pwm_type = {
    { &mp_type_type },
    .name = MP_QSTR_PWM,
    .print = esp32_pwm_print,
    .make_new = esp32_pwm_make_new,
    .locals_dict = (mp_obj_dict_t*)&esp32_pwm_locals_dict,
};
