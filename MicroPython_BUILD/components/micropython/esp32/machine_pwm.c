/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
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
#include "modmachine.h"
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
    uint8_t timer;
} esp32_pwm_obj_t;

STATIC esp32_pwm_obj_t *pwm_channels[LEDC_CHANNEL_MAX];
STATIC ledc_timer_config_t pwm_timers[LEDC_CHANNEL_MAX/2];
STATIC uint8_t pwm_is_init = 0;


//-----------------------------------------------------
STATIC float duty2perc(esp32_pwm_obj_t *self, int duty)
{
	return (float)((duty * 10000) / (1 << pwm_timers[self->timer].duty_resolution)) / 100.0;
}

//------------------------------------------------------
STATIC int perc2duty(esp32_pwm_obj_t *self, float dperc)
{
	return ((int)(dperc * 100.0) * (1 << pwm_timers[self->timer].duty_resolution)) / 10000;
}

//----------------------------------------------------
STATIC int set_freq(esp32_pwm_obj_t *self, int newval)
{
	if (newval == pwm_timers[self->timer].freq_hz) return 1;

    // Adjust duty resolution for new frequency if needed
    int dres = 15;
    while (newval > (80000000 / (1 << dres))) {
    	dres--;
    	if (dres == 0) break;
    }
    if (dres == 0) return 0; // can't set duty resolution for the requested frequency

	int n;
	float dperc[LEDC_CHANNEL_MAX];
	// get duty percentage for all channels using the same timer
	for (n=0; n<LEDC_CHANNEL_MAX; n++) {
		dperc[n] = -1;
		if (pwm_channels[n]) {
			if (pwm_channels[n]->timer == self->timer) {
				int old_duty = ledc_get_duty(PWMODE, pwm_channels[n]->channel);
				dperc[n] = duty2perc(pwm_channels[n], old_duty);
			}
		}
	}

	// Get old frequency and duty resolution
    int old_freq = pwm_timers[self->timer].freq_hz;
    int old_dres = pwm_timers[self->timer].duty_resolution;

    pwm_timers[self->timer].duty_resolution = dres;
    pwm_timers[self->timer].freq_hz = newval;
    if (ledc_timer_config(&(pwm_timers[self->timer])) != ESP_OK) {
    	pwm_timers[self->timer].freq_hz = old_freq;
    	pwm_timers[self->timer].duty_resolution = old_dres;
        return 0;
    }

    if (dres != old_dres) {
    	// Adjust duty cycle if duty resolution was changed
        for (n=0; n<LEDC_CHANNEL_MAX; n++) {
			if (dperc[n] >= 0) {
				int duty = perc2duty(pwm_channels[n], dperc[n]);
				ledc_set_duty(PWMODE, pwm_channels[n]->channel, duty);
				ledc_update_duty(PWMODE, pwm_channels[n]->channel);
			}
        }
    }

    return 1;
}

//------------------------------------------------
STATIC void pwm_initChannel(esp32_pwm_obj_t *self)
{
	// Initialize the pwm timer
	if (ledc_timer_config(&(pwm_timers[self->timer])) != ESP_OK) {
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Error configuring pwm timer %d", self->timer));
	}

	ledc_channel_config_t cfg = {
		.channel = self->channel,
		.duty = perc2duty(self, 50.0),
		.gpio_num = self->pin,
		.intr_type = LEDC_INTR_DISABLE,
		.speed_mode = PWMODE,
		.timer_sel = pwm_timers[self->timer].timer_num,
	};
	if (ledc_channel_config(&cfg) != ESP_OK) {
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Error configuring channel for pin %d", self->pin));
	}
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
        float dperc = duty2perc(self, duty);
        mp_printf(print, ", freq=%u Hz, duty=%.2f%% [%d], duty resolution=%d bits, channel=%d, timer=%d",
        		ledc_get_freq(PWMODE, pwm_timers[self->timer].timer_num), dperc, duty,
				pwm_timers[self->timer].duty_resolution, self->channel, pwm_timers[self->timer].timer_num);
    }
    else mp_printf(print, ", not active");
    mp_printf(print, ")");
}

//------------------------------------------------------------------------------------------------------------------
STATIC void esp32_pwm_init_helper(esp32_pwm_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_freq, ARG_duty, ARG_timer };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_timer, MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int timer = args[ARG_timer].u_int;

    if (self->channel >= LEDC_CHANNEL_MAX) {
		// === New PWM assignment
        if (timer < 0) timer = 0;
        if (timer > 3) {
    		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "wrong timer requested: %d (0~3 allowed)", timer));
        }
        self->timer = timer;
        int channel;
        // Find a free PWM channel ===
        for (channel = 0; channel < LEDC_CHANNEL_MAX; ++channel) {
			if (pwm_channels[channel] == NULL) break;
		}
		if (channel >= LEDC_CHANNEL_MAX) {
			mp_raise_ValueError("out of PWM channels");
		}
		// Check if the requested pin already used by other PWM
		for (int chan = 0; chan < LEDC_CHANNEL_MAX; ++chan) {
			if (pwm_channels[channel]) {
				if (pwm_channels[chan]->pin == self->pin) {
					nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "pin %d already used for pwm", self->pin));
				}
			}
		}
		self->channel = channel;

		pwm_initChannel(self);

		pwm_channels[channel] = self;
		self->active = 1;
    }


    // Check if pwm timer has to be changed
    if ((timer >= 0) && (timer < 4)) {
        if (timer != pwm_timers[self->timer].timer_num) {
            int old_duty = ledc_get_duty(PWMODE, self->channel);
            float old_dperc = duty2perc(self, old_duty);
            int old_freq = pwm_timers[self->timer].freq_hz;

            ledc_stop(PWMODE, self->channel, 0);
            self->timer = timer;

    		pwm_initChannel(self);

    		if (old_freq != pwm_timers[self->timer].freq_hz) {
				if (!set_freq(self, old_freq)) {
					nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Error setting frequency %d for new timer", old_freq));
				}
    		}
            int new_duty = ledc_get_duty(PWMODE, self->channel);
    		if (old_duty != new_duty) {
				ledc_set_duty(PWMODE, self->channel, perc2duty(self, old_dperc));
				ledc_update_duty(PWMODE, self->channel);
    		}
        }
    }

    // Check if the frequency has to be changed
    int fqval = args[ARG_freq].u_int;
    if (fqval != -1) {
        if (fqval != pwm_timers[self->timer].freq_hz) {
            if (!set_freq(self, fqval)) {
                nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad frequency %d", fqval));
            }
        }
    }

    // Check if the duty cycle has to be changed
    if (args[ARG_duty].u_obj != mp_const_none) {
    	float dperc = mp_obj_get_float(args[ARG_duty].u_obj);
    	if ((dperc < 0) || (dperc > 100-0)) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad duty %.2f, use 0 ~ 100 (%%)", dperc));
    	}
    	// Calculate duty value
        ledc_set_duty(PWMODE, self->channel, perc2duty(self, dperc));
        ledc_update_duty(PWMODE, self->channel);
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

    if (!pwm_is_init) {
    	int n;
		// Set default timers configuration
    	for (n=0; n<(LEDC_CHANNEL_MAX/2); n++) {
    		pwm_timers[n].duty_resolution = LEDC_TIMER_12_BIT;	// 12 bits duty resolution
    		pwm_timers[n].freq_hz = 5000;							// 5 kHz frequency
    		pwm_timers[n].speed_mode = PWMODE;
    		pwm_timers[n].timer_num = n;
    	}
    	// Init channels
    	for (n=0; n<(LEDC_CHANNEL_MAX); n++) {
    		pwm_channels[n] = NULL;
    	}
    	pwm_is_init = 1;
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
    if ((chan >= 0) && (chan < LEDC_CHANNEL_MAX)) {
        // Mark it unused, and tell the hardware to stop routing
        pwm_channels[chan] = NULL;
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

    int curr_freq = pwm_timers[self->timer].freq_hz;
    if (n_args == 1) {
        // get and return frequency
        return MP_OBJ_NEW_SMALL_INT(curr_freq);
    }

    // set the frequency
    int new_freq = mp_obj_get_int(args[1]);
    if (new_freq != curr_freq) {
		if (!set_freq(self, new_freq)) {
			nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad frequency %d", new_freq));
		}
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_pwm_freq_obj, 1, 2, esp32_pwm_freq);

//-----------------------------------------------------------------
STATIC mp_obj_t esp32_pwm_duty(size_t n_args, const mp_obj_t *args)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    if (n_args == 1) {
        // get and return duty cycle
        int duty = ledc_get_duty(PWMODE, self->channel);
        return mp_obj_new_float(duty2perc(self, duty));
    }

    // set the new duty cycle
    float dperc = mp_obj_get_float(args[1]);
	if ((dperc < 0) || (dperc > 100.0)) {
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Bad duty %d, use 0 ~ 100 (%%)", dperc));
	}
	ledc_set_duty(PWMODE, self->channel, perc2duty(self, dperc));
	ledc_update_duty(PWMODE, self->channel);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_pwm_duty_obj, 1, 2, esp32_pwm_duty);

//-----------------------------------------------
STATIC mp_obj_t esp32_pwm_pause(mp_obj_t self_in)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ledc_timer_pause(PWMODE, pwm_timers[self->timer].timer_num);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pwm_pause_obj, esp32_pwm_pause);

//------------------------------------------------
STATIC mp_obj_t esp32_pwm_resume(mp_obj_t self_in)
{
    esp32_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    ledc_timer_resume(PWMODE, pwm_timers[self->timer].timer_num);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_pwm_resume_obj, esp32_pwm_resume);

//------------------------------
STATIC mp_obj_t esp32_pwm_list()
{
	for (int n=0; n<LEDC_CHANNEL_MAX; n++) {
		if (pwm_channels[n]) {
			esp32_pwm_print(&mp_plat_print, (mp_obj_t)pwm_channels[n], 0);
			mp_printf(&mp_plat_print, "\n");
		}
	}

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(esp32_pwm_list_obj, esp32_pwm_list);



//==============================================================
STATIC const mp_rom_map_elem_t esp32_pwm_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&esp32_pwm_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&esp32_pwm_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_freq), MP_ROM_PTR(&esp32_pwm_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_duty), MP_ROM_PTR(&esp32_pwm_duty_obj) },
    { MP_ROM_QSTR(MP_QSTR_pause), MP_ROM_PTR(&esp32_pwm_pause_obj) },
    { MP_ROM_QSTR(MP_QSTR_resume), MP_ROM_PTR(&esp32_pwm_resume_obj) },
    { MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&esp32_pwm_list_obj) },
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
