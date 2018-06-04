/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Based on original 'machine-timer.c' from https://github.com/micropython/micropython-esp32
 *   completely rewritten and many new functions and features added
 *
 * Copyright (c) 2017 Boris Lovosevic (https://github.com/loboris)
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

#include "driver/timer.h"
#include "py/runtime.h"
#include "modmachine.h"

#define TIMER_INTR_SEL		TIMER_INTR_LEVEL
#define TIMER_DIVIDER_KHZ	40000	// 500 us per tick, 1 kHz
#define TIMER_DIVIDER_MHZ	80		// 1 us per tick, 1 MHz
#define TIMER_SCALE_KHZ		2
#define TIMER_SCALE_MHZ		100

#define TIMER_RUNNING	1
#define TIMER_PAUSED	0

#define TIMER_TYPE_ONESHOT	0
#define TIMER_TYPE_PERIODIC	1
#define TIMER_TYPE_CHRONO	2
#define TIMER_TYPE_EXTBASE	3
#define TIMER_TYPE_EXT		4
#define TIMER_TYPE_MAX		5

#define TIMER_EXT_NUM		8
#define TIMER_FLAGS			0


const mp_obj_type_t machine_timer_type;

machine_timer_obj_t * mpy_timers_used[4] = {NULL};
static machine_timer_obj_t * ext_timers[TIMER_EXT_NUM] = {NULL};


//----------------------------------------------
STATIC esp_err_t check_esp_err(esp_err_t code) {
    if (code) {
        mp_raise_OSError(code);
    }

    return code;
}

//----------------------------------------------------------------------------------------------
STATIC void machine_timer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    machine_timer_obj_t *self = self_in;

    mp_printf(print, "Timer(%d) ", self->id);

    if (self->type >= TIMER_TYPE_MAX) {
        mp_printf(print, "Not initialized");
        return;
    }

    char stype[16];
    if (self->type == TIMER_TYPE_PERIODIC) sprintf(stype, "Periodic");
    else if (self->type == TIMER_TYPE_ONESHOT) sprintf(stype, "One shot");
    else if (self->type == TIMER_TYPE_CHRONO) sprintf(stype, "Chrono");
    else if (self->type == TIMER_TYPE_EXTBASE) sprintf(stype, "Extended base");
    else if (self->type == TIMER_TYPE_EXT) sprintf(stype, "Extended");
    else sprintf(stype, "Unknown");

    if (self->type == TIMER_TYPE_CHRONO) {
    	mp_printf(print, "Period: 1 us; ");
    }
    else if (self->type == TIMER_TYPE_EXT) {
    	mp_printf(print, "Period: %d ms; ", self->period);
    }
    else {
    	mp_printf(print, "Period: %d ms; ", self->period / 2);
    }
    if (self->type != TIMER_TYPE_CHRONO) {
    	mp_printf(print, "Repeat: %s; ", (self->repeat ? "True" : "False"));
    }
    mp_printf(print, "Type: %s; ", stype);
    mp_printf(print, "Running: %s\n", (self->state == TIMER_RUNNING) ? "yes" : "no");
    if ((self->type != TIMER_TYPE_CHRONO) && (self->type != TIMER_TYPE_EXTBASE)) {
        mp_printf(print, "         Events: %lu", self->event_num);
        mp_printf(print, "; Callbacks: %lu; ", self->cb_num);
    	mp_printf(print, "Missed: %lu", self->event_num - self->cb_num);
    }
    if (self->debug_pin >= 0) {
        mp_printf(print, "\n         Debug output on gpio %d, ", self->debug_pin);
    }
    if (self->type == TIMER_TYPE_EXTBASE) {
        machine_timer_obj_t *extmr;
        mp_printf(print, "  Handled extended timers:\n");
		for (int i=0; i < TIMER_EXT_NUM; i++) {
			extmr = ext_timers[i];
			if (extmr) {
				mp_printf(print, "    %2d: Period: %d ms, %s", i+4, extmr->period, (extmr->state == TIMER_RUNNING) ? "Running" : "Paused");
			}
		}
    }

}

//-----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_timer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    machine_timer_obj_t *self = m_new_obj(machine_timer_obj_t);
    self->base.type = &machine_timer_type;

    self->callback = NULL;
    self->handle = NULL;
    self->event_num = 0;
    self->cb_num = 0;
    self->debug_pin = -1;
	self->state = TIMER_PAUSED;
	self->type = TIMER_TYPE_MAX;

    int tmr = mp_obj_get_int(args[0]);
    if ((tmr < 0) || (tmr > 11)) {
    	mp_raise_ValueError("Only base timers 0~3 and extended timers 4~11 can be used.");
    }
    if (tmr < 4) {
    	// Base hardware timer
    	if ((tmr == ADC_TIMER_NUM) && (adc_timer_active)) {
        	mp_raise_ValueError("Timer used by ADC module.");
    	}
        if (mpy_timers_used[tmr] != NULL) {
        	mp_raise_ValueError("Timer already in use.");
        }
        mpy_timers_used[tmr] = self;
    }
    else {
    	// Extended timer
    	if ((mpy_timers_used[0] == NULL) || (mpy_timers_used[0]->type != TIMER_TYPE_EXTBASE)) {
			mp_raise_ValueError("Timer 0 not configured as extended timer.");
    	}
    	if (ext_timers[(tmr-4)] != NULL) {
        	mp_raise_ValueError("Extended Timer already in use.");
    	}
    	ext_timers[(tmr-4)] = self;
    }
    self->id = tmr;

    return self;
}

//----------------------------------------------------------
STATIC void machine_timer_disable(machine_timer_obj_t *self)
{
    if ((self->id < 4) && (self->handle)) {
        timer_pause((self->id >> 1) & 1, self->id & 1);
        if (self->type != TIMER_TYPE_CHRONO) esp_intr_free(self->handle);
        mpy_timers_used[self->id] = NULL;
    }
    else ext_timers[(self->id-4)] = NULL;
    self->callback = NULL;
    self->handle = NULL;
    self->event_num = 0;
    self->cb_num = 0;
    self->debug_pin = -1;
	self->state = TIMER_PAUSED;
	self->type = TIMER_TYPE_MAX;
}

//------------------------------------------
STATIC void machine_timer_isr(void *self_in)
{
    machine_timer_obj_t *self = (machine_timer_obj_t *)self_in;

    if (self->id & 2) {
    	if (self->id & 1) TIMERG1.int_clr_timers.t1 = 1;
    	else TIMERG1.int_clr_timers.t0 = 1;
    	TIMERG1.hw_timer[self->id & 1].config.alarm_en = self->repeat;
    }
    else {
    	if (self->id & 1) TIMERG0.int_clr_timers.t1 = 1;
    	else TIMERG0.int_clr_timers.t0 = 1;
    	TIMERG0.hw_timer[self->id & 1].config.alarm_en = self->repeat;
    }

    if (self->debug_pin >= 0) {
    	if (self->debug_pin_mode >= 0) gpio_set_level(self->debug_pin, (self->debug_pin_mode & 1));
    	else gpio_set_level(self->debug_pin, (self->event_num & 1));
    }
    self->event_num++;

    if ((self->callback) && (mp_sched_schedule(self->callback, self, NULL))) self->cb_num++;
}

//----------------------------------------------
STATIC void machine_ext_timer_isr(void *self_in)
{
	// extended timer interrupt is fired every 1 ms
    machine_timer_obj_t *self = (machine_timer_obj_t *)self_in;
    machine_timer_obj_t *extmr;

    TIMERG0.int_clr_timers.t0 = 1;
    TIMERG0.hw_timer[0].config.alarm_en = 1;

    self->event_num++;
    // test extended timers
    for (int i=0; i < TIMER_EXT_NUM; i++) {
		extmr = ext_timers[i];
    	if (extmr) {
    		// Extended timer exists
    		if (extmr->state == TIMER_RUNNING) {
    			// Timer is running, increment extended timer's counter
				extmr->counter++;
				if ((extmr->counter % extmr->period) == 0) {
					// Extended timer's period elapsed, increment events number
				    extmr->event_num++;
					if (extmr->counter == extmr->alarm) {
						// Schedule the callback execution
						if ((extmr->callback) && (mp_sched_schedule(extmr->callback, extmr, NULL))) {
							extmr->cb_num++;
							self->cb_num++;
						}
						if (extmr->repeat) extmr->counter = 0x00000000ULL;
					}
				}
    		}
    	}
    }
}

//---------------------------------------------------------
STATIC void machine_timer_enable(machine_timer_obj_t *self)
{
	if (self->id >= 4) {
		ext_timers[(self->id-4)] = self;
		return;
	}

	timer_config_t config;
    config.counter_dir = TIMER_COUNT_UP;
    config.intr_type = TIMER_INTR_LEVEL;
    config.counter_en = TIMER_PAUSE;

    if (self->type == TIMER_TYPE_CHRONO) {
        config.alarm_en = TIMER_ALARM_DIS;
        config.auto_reload = TIMER_AUTORELOAD_DIS;
    	config.divider = TIMER_DIVIDER_MHZ;
    }
    else {
        config.alarm_en = TIMER_ALARM_EN;
        config.auto_reload = self->repeat;
    	config.divider = TIMER_DIVIDER_KHZ;
    }

    check_esp_err(timer_init((self->id >> 1) & 1, self->id & 1, &config));

    // Timer's counter will initially start from value below.
    // Also, if auto_reload is set, this value will be automatically reload on alarm

    check_esp_err(timer_set_counter_value((self->id >> 1) & 1, self->id & 1, 0x00000000ULL));

    if (self->type != TIMER_TYPE_CHRONO) {
		// Configure the alarm value and the interrupt on alarm.
		check_esp_err(timer_set_alarm_value((self->id >> 1) & 1, self->id & 1, self->period));
		// Enable timer interrupt
		check_esp_err(timer_enable_intr((self->id >> 1) & 1, self->id & 1));
		// Register interrupt callback
		if (self->type == TIMER_TYPE_EXTBASE) {
			check_esp_err(timer_isr_register((self->id >> 1) & 1, self->id & 1, machine_ext_timer_isr, (void*)self, TIMER_FLAGS, &self->handle));
		}
		else {
			check_esp_err(timer_isr_register((self->id >> 1) & 1, self->id & 1, machine_timer_isr, (void*)self, TIMER_FLAGS, &self->handle));
		}
    	check_esp_err(timer_start((self->id >> 1) & 1, self->id & 1));
    }
    mpy_timers_used[self->id] = self;
}

//---------------------------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_timer_init_helper(machine_timer_obj_t *self, mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_period,       MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_mode,         MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = TIMER_TYPE_PERIODIC} },
        { MP_QSTR_callback,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_dbgpin,       MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_dbgpinmode,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };

    machine_timer_disable(self);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    self->handle = NULL;
    self->event_num = 0;
    self->cb_num = 0;
    self->debug_pin = -1;

    if (self->id < 4) {
    	// Base hardware timer
		self->type = args[1].u_int & 3;

		if (self->type == TIMER_TYPE_EXTBASE) {
			if (self->id != 0) {
				mp_raise_ValueError("Only timer 0 can be used as extended timer.");
			}
			// Extended base timer types use an 2 kHz clock
			// set the period to 1 ms, repeat mode, no callback
			self->period = 2;
			self->repeat = 1;
			self->callback = NULL;
		}
		else if (self->type == TIMER_TYPE_CHRONO) {
			// Chrono Timer uses an 1 MHz clock, no callback
			// set period in us
			self->period = 1;
			self->repeat = 0;
			self->callback = NULL;
		}
		else {
			// Other timer types use an 2 kHz clock, convert period to ms.
			if (args[0].u_int < 1) self->period = 2;
			else self->period = args[0].u_int * 2;
			self->repeat = args[1].u_int & 1;
			if (args[2].u_obj != mp_const_none) self->callback = args[2].u_obj;
			// set the debug gpio if used
			if ((args[3].u_int >= 0) && (args[3].u_int < 34)) {
				self->debug_pin = args[3].u_int;
				self->debug_pin_mode = args[4].u_int;
				gpio_pad_select_gpio(self->debug_pin);
				gpio_set_direction(self->debug_pin, GPIO_MODE_OUTPUT);
				gpio_set_level(self->debug_pin, 0);
			}
		}
		// enable and start the timer
		machine_timer_enable(self);

		if (self->type != TIMER_TYPE_CHRONO) self->state = TIMER_RUNNING;
		else self->state = TIMER_PAUSED;
    }
    else {
    	// Extended timer
    	uint8_t mode = args[1].u_int & 3;
    	if ((mode != TIMER_TYPE_PERIODIC) && (mode != TIMER_TYPE_ONESHOT)) {
			mp_raise_ValueError("Wrong mode for extended timer.");
    	}
    	self->type = TIMER_TYPE_EXT;
		if (args[0].u_int < 1) self->period = 1;
		else self->period = args[0].u_int;
		self->repeat = args[1].u_int & 1;
		if (args[2].u_obj != mp_const_none) self->callback = args[2].u_obj;
		self->counter = 0x00000000ULL;
		self->alarm = self->period;
		self->state = TIMER_RUNNING;

		machine_timer_enable(self);
    }

    return mp_const_none;
}

//-------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_timer_init(mp_uint_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    return machine_timer_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_timer_init_obj, 1, machine_timer_init);

//----------------------------------------------------
STATIC mp_obj_t machine_timer_deinit(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;
    if ((self->id < 4) && (self->type == TIMER_TYPE_EXTBASE)) {
    	// Check if any extended timer exists
    	uint8_t num_ext = 0;
        for (int i=0; i < TIMER_EXT_NUM; i++) {
        	if (ext_timers[i] != NULL) num_ext++;
        }
        if (num_ext) {
        	mp_raise_msg(&mp_type_OSError, "Can't deinit extended base timer, some timers running.");
        }
    }
    machine_timer_disable(self_in);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_deinit_obj, machine_timer_deinit);

//---------------------------------------------------
STATIC mp_obj_t machine_timer_value(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;
    uint64_t result;

    if (self->id < 4) {
    	// Base hardware timer
		if (self->type == TIMER_TYPE_EXTBASE) result = self->event_num;  // value in ms
		else {
			timer_get_counter_value((self->id >> 1) & 1, self->id & 1, &result);
			if (self->type != TIMER_TYPE_CHRONO) result *= (self->period / 2);  // value in us
		}
    }
    else {
    	// Extended timer
    	result = self->counter;  // value in ms
    }
    return mp_obj_new_int_from_ull(result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_value_obj, machine_timer_value);

//---------------------------------------------------
STATIC mp_obj_t machine_timer_pause(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;

    if (self->id < 4) {
    	// Base hardware timer
        if (self->type == TIMER_TYPE_EXTBASE) {
        	// Check if any extended timer is running
        	uint8_t num_ext = 0;
            for (int i=0; i < TIMER_EXT_NUM; i++) {
            	if ((ext_timers[i] != NULL) && (ext_timers[i]->state == TIMER_RUNNING))  num_ext++;
            }
            if (num_ext) {
            	mp_raise_msg(&mp_type_OSError, "Can't pause extended base timer, some timers running.");
            }
        }
		if (self->state == TIMER_RUNNING) {
			check_esp_err(timer_pause((self->id >> 1) & 1, self->id & 1));
			self->state = TIMER_PAUSED;
		}
    }
    else self->state = TIMER_PAUSED;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_pause_obj, machine_timer_pause);

//----------------------------------------------------
STATIC mp_obj_t machine_timer_resume(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;

    if (self->id < 4) {
    	// Base hardware timer
		if (self->state == TIMER_PAUSED) {
			check_esp_err(timer_start((self->id >> 1) & 1, self->id & 1));
			self->state = TIMER_RUNNING;
		}
    }
    else {
		if ((mpy_timers_used[0] != NULL) && (mpy_timers_used[0]->state == TIMER_RUNNING)) self->state = TIMER_RUNNING;
		else self->state = TIMER_PAUSED;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_resume_obj, machine_timer_resume);

//---------------------------------------------------
STATIC mp_obj_t machine_timer_start(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;

    if (self->type == TIMER_TYPE_CHRONO) {
        if (self->state == TIMER_RUNNING) {
		    check_esp_err(timer_pause((self->id >> 1) & 1, self->id & 1));
        }
        self->event_num = 0;
		check_esp_err(timer_set_counter_value((self->id >> 1) & 1, self->id & 1, 0x00000000ULL));
		check_esp_err(timer_start((self->id >> 1) & 1, self->id & 1));
		self->state = TIMER_RUNNING;
	}
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_start_obj, machine_timer_start);

//---------------------------------------------------------------------
STATIC mp_obj_t machine_timer_shot(size_t n_args, const mp_obj_t *args)
{
    machine_timer_obj_t *self = args[0];

    if (self->type != TIMER_TYPE_ONESHOT) {
    	mp_raise_ValueError("Timer is not one_shot timer.");
    }

    int tmo = self->period;
    if (n_args > 1) tmo = mp_obj_get_int(args[1]) * 2;
    if (tmo < 1) tmo = self->period;
    uint64_t cnt_val;

    check_esp_err(timer_pause((self->id >> 1) & 1, self->id & 1));
    check_esp_err(timer_get_counter_value((self->id >> 1) & 1, self->id & 1, &cnt_val));
    check_esp_err(timer_set_alarm_value((self->id >> 1) & 1, self->id & 1, cnt_val+tmo));
    check_esp_err(timer_start((self->id >> 1) & 1, self->id & 1));
    if (self->id & 2) {
    	TIMERG1.hw_timer[self->id & 1].config.alarm_en = 1;
    }
    else {
    	TIMERG0.hw_timer[self->id & 1].config.alarm_en = 1;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_timer_shot_obj, 1, 2, machine_timer_shot);

//------------------------------------------------
STATIC mp_obj_t machine_timer_id(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;

    return MP_OBJ_NEW_SMALL_INT(self->id);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_id_obj, machine_timer_id);

//----------------------------------------------------
STATIC mp_obj_t machine_timer_events(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;
    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int_from_ull(self->event_num);
    tuple[1] = mp_obj_new_int_from_ull(self->cb_num);

    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_events_obj, machine_timer_events);

//-------------------------------------------------------
STATIC mp_obj_t machine_timer_isrunning(mp_obj_t self_in)
{
    machine_timer_obj_t *self = self_in;

    if (self->state == TIMER_RUNNING) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_timer_isrunning_obj, machine_timer_isrunning);

//-----------------------------------------------------------------------
STATIC mp_obj_t machine_timer_period(size_t n_args, const mp_obj_t *args)
{
    machine_timer_obj_t *self = args[0];
    uint32_t period = 1;

    if (n_args > 1) {
		if (self->type == TIMER_TYPE_EXTBASE) return MP_OBJ_NEW_SMALL_INT(1);

		uint8_t old_state = self->state;
		// Pause timer
		if (self->id < 4) {
			// Base hardware timer
			if (self->state == TIMER_RUNNING) {
				check_esp_err(timer_pause((self->id >> 1) & 1, self->id & 1));
			}
		}
		self->state = TIMER_PAUSED;

		// Set new period
		period = mp_obj_get_int(args[1]);
		if (self->type == TIMER_TYPE_CHRONO) {
			if (period < 10) period = 1;
			else period /= 10;
		}
		else if (self->type == TIMER_TYPE_EXT) {
			if (period < 1) period = 1;
			self->counter = 0x00000000ULL;
			self->alarm = period;
		}
		else {
			if (period < 1) period = 2;
			else period *= 2;
		}
		self->period = period;
		if (self->id < 4) {
			check_esp_err(timer_set_counter_value((self->id >> 1) & 1, self->id & 1, 0x00000000ULL));
			check_esp_err(timer_set_alarm_value((self->id >> 1) & 1, self->id & 1, self->period));
		}

		if ((self->id < 4) && (old_state == TIMER_RUNNING)){
			// Base hardware timer
			check_esp_err(timer_start((self->id >> 1) & 1, self->id & 1));
		}
		self->state = old_state;
    }

    if (self->type == TIMER_TYPE_CHRONO) period = self->period * 10;
    else if (self->type == TIMER_TYPE_EXT) period = self->period;
    else period = self->period / 2;

    return MP_OBJ_NEW_SMALL_INT(period);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_timer_period_obj, 1, 2, machine_timer_period);

//-------------------------------------------------------------------------
STATIC mp_obj_t machine_timer_callback(size_t n_args, const mp_obj_t *args)
{
    machine_timer_obj_t *self = args[0];

    if (n_args == 1) {
    	if (self->callback == NULL) return mp_const_false;
    	return mp_const_true;
    }

    if ((self->type == TIMER_TYPE_EXTBASE) || (self->type == TIMER_TYPE_CHRONO)) return mp_const_false;

    uint8_t old_state = self->state;
    // Pause timer
    if (self->id < 4) {
    	// Base hardware timer
		if (self->state == TIMER_RUNNING) {
			check_esp_err(timer_pause((self->id >> 1) & 1, self->id & 1));
		}
    }
    self->state = TIMER_PAUSED;

    if ((MP_OBJ_IS_FUN(args[1])) || (MP_OBJ_IS_METH(args[1]))) {
		// Set new callback
		self->counter = 0x00000000ULL;
		self->alarm = self->period;
		self->callback = args[1];
    }
    else self->callback = NULL;


    if ((self->id < 4) && (old_state == TIMER_RUNNING)){
    	// Base hardware timer
		check_esp_err(timer_start((self->id >> 1) & 1, self->id & 1));
    }
	self->state = old_state;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_timer_callback_obj, 1, 2, machine_timer_callback);

//==============================================================
STATIC const mp_map_elem_t machine_timer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__),		(mp_obj_t)&machine_timer_deinit_obj },
    { MP_ROM_QSTR(MP_QSTR_deinit),		(mp_obj_t)&machine_timer_deinit_obj },
    { MP_ROM_QSTR(MP_QSTR_init),		(mp_obj_t)&machine_timer_init_obj },
    { MP_ROM_QSTR(MP_QSTR_value),		(mp_obj_t)&machine_timer_value_obj },
    { MP_ROM_QSTR(MP_QSTR_events),		(mp_obj_t)&machine_timer_events_obj },
    { MP_ROM_QSTR(MP_QSTR_reshoot),		(mp_obj_t)&machine_timer_shot_obj },
    { MP_ROM_QSTR(MP_QSTR_start),		(mp_obj_t)&machine_timer_start_obj },
    { MP_ROM_QSTR(MP_QSTR_stop),		(mp_obj_t)&machine_timer_pause_obj },
    { MP_ROM_QSTR(MP_QSTR_pause),		(mp_obj_t)&machine_timer_pause_obj },
    { MP_ROM_QSTR(MP_QSTR_resume),		(mp_obj_t)&machine_timer_resume_obj },
    { MP_ROM_QSTR(MP_QSTR_timernum),	(mp_obj_t)&machine_timer_id_obj },
    { MP_ROM_QSTR(MP_QSTR_period),		(mp_obj_t)&machine_timer_period_obj },
    { MP_ROM_QSTR(MP_QSTR_callback),	(mp_obj_t)&machine_timer_callback_obj },
    { MP_ROM_QSTR(MP_QSTR_isrunning),	(mp_obj_t)&machine_timer_isrunning_obj },

	{ MP_ROM_QSTR(MP_QSTR_ONE_SHOT),	MP_ROM_INT(TIMER_TYPE_ONESHOT) },
    { MP_ROM_QSTR(MP_QSTR_PERIODIC),	MP_ROM_INT(TIMER_TYPE_PERIODIC) },
    { MP_ROM_QSTR(MP_QSTR_CHRONO),		MP_ROM_INT(TIMER_TYPE_CHRONO) },
    { MP_ROM_QSTR(MP_QSTR_EXTBASE),		MP_ROM_INT(TIMER_TYPE_EXTBASE) },
    { MP_ROM_QSTR(MP_QSTR_EXTENDED),	MP_ROM_INT(TIMER_TYPE_EXTBASE) },
};
STATIC MP_DEFINE_CONST_DICT(machine_timer_locals_dict, machine_timer_locals_dict_table);

//========================================
const mp_obj_type_t machine_timer_type = {
    { &mp_type_type },
    .name = MP_QSTR_Timer,
    .print = machine_timer_print,
    .make_new = machine_timer_make_new,
    .locals_dict = (mp_obj_t)&machine_timer_locals_dict,
};
