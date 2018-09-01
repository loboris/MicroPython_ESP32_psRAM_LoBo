/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
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
#include <string.h>
#include "driver/rtc_io.h"
#include "driver/gpio.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "extmod/virtpin.h"
#include "modmachine.h"

extern bool mpy_use_spiram;
extern int MainTaskCore;

static const uint8_t pin_modes[5] = {
	GPIO_MODE_INPUT,
	GPIO_MODE_OUTPUT,
	GPIO_MODE_OUTPUT_OD,
	GPIO_MODE_INPUT_OUTPUT_OD,
	GPIO_MODE_INPUT_OUTPUT
};
static const uint8_t pin_pull_modes[4] = {
	GPIO_PULLUP_ONLY,
	GPIO_PULLDOWN_ONLY,
	GPIO_PULLUP_PULLDOWN,
	GPIO_FLOATING
};


//----------------------------------------------
gpio_num_t machine_pin_get_id(mp_obj_t pin_in) {
    if (mp_obj_get_type(pin_in) != &machine_pin_type) {
        mp_raise_ValueError("expecting a pin");
    }
    machine_pin_obj_t *self = pin_in;
    return self->id;
}

//---------------------------------------
int machine_pin_get_gpio(mp_obj_t pin_in)
{
    if (MP_OBJ_IS_INT(pin_in)) {
    	int wanted_pin = mp_obj_get_int(pin_in);
    	if (!GPIO_IS_VALID_GPIO(wanted_pin)) {
        	mp_raise_ValueError("Invalid pin number");
    	}
    	return wanted_pin;
    }
    return machine_pin_get_id(pin_in);
}

//----------------------------
void machine_pins_init(void) {
    static bool did_install = false;
    if (!did_install) {
        gpio_install_isr_service(0);
        did_install = true;
    }

    machine_rtc_config.ext0_pin = -1;
    for (int i=0; i<EXT1_WAKEUP_MAX_PINS; i++) {
    	machine_rtc_config.ext1_pins[i] = -1;
    }
}

//------------------------------
void machine_pins_deinit(void) {
	/*
    for (int i = 0; i < MP_ARRAY_SIZE(machine_pin_obj); ++i) {
        if (machine_pin_obj[i].id != (gpio_num_t)-1) {
            gpio_isr_handler_remove(machine_pin_obj[i].id);
        }
    }
    */
}

//---------------------------------------------------
static void _pin_disable_irq(machine_pin_obj_t *self)
{
	gpio_isr_handler_remove(self->id);
    self->irq_handler = NULL;
    self->irq_type = GPIO_PIN_INTR_DISABLE;
    self->irq_debounce = 0;
    self->irq_active_time = 0;
	self->irq_retvalue = -1;
	self->irq_any_level = gpio_get_level(self->id);
}

//--------------------------------------------------
static void _pin_enable_irq(machine_pin_obj_t *self)
{
	if (self->irq_type == GPIO_INTR_ANYEDGE) {
		// use level interrupts for 'any edge' interrupt type!
		self->irq_any_level = gpio_get_level(self->id);
		gpio_set_intr_type(self->id, self->irq_any_level ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
	}
	else gpio_set_intr_type(self->id, self->irq_type);
}

// gpio interrupt is disabled on entry!
//--------------------------------------------
static void debounce_task(void *pvParameters)
{
	machine_pin_obj_t *self = (machine_pin_obj_t *)pvParameters;

    // get current pin level
    uint8_t levl = 0;
    uint8_t active_level = 0;
    if (self->irq_type == GPIO_INTR_POSEDGE) active_level = 1;
    else if (self->irq_type == GPIO_INTR_NEGEDGE) active_level = 0;
    else if (self->irq_type == GPIO_INTR_ANYEDGE) active_level = self->irq_any_level;

    int64_t curr_time = mp_hal_ticks_us();
    int64_t start_time = curr_time;
    int64_t total_time = 0;
    int32_t loop_time = 0;
    int32_t active_time = 0;

    if (self->irq_active_time > 0) {
        // Pin must on active level for some time
        while (total_time < self->irq_active_time) {
            ets_delay_us(100);
            curr_time = mp_hal_ticks_us();
            loop_time = (curr_time - start_time);
            start_time = curr_time;
            total_time += loop_time;
            active_time += loop_time;

            levl = gpio_get_level(self->id);
            if (levl == active_level) active_time += loop_time;
            else active_time = 0;

            if (active_time >= self->irq_active_time) {
                self->irq_retvalue = levl;
                if (self->irq_handler) {
                    // schedule the callback function
                    mp_sched_schedule(self->irq_handler, MP_OBJ_FROM_PTR(self), NULL);
                }
                break;
            }
            vTaskDelay(0);
        }
    }

    int32_t remaining_time = self->irq_debounce - total_time;
    if (remaining_time > 0) {
        if (remaining_time > 2000) {
            uint32_t dus = remaining_time % 2000;	// remaining micro seconds
            vTaskDelay(remaining_time/2000);
            if (dus) ets_delay_us(dus);
        }
        else ets_delay_us(remaining_time);
    }

	if (self->irq_handler) {
		// schedule the callback function
        self->irq_retvalue = gpio_get_level(self->id);
		mp_sched_schedule(self->irq_handler, MP_OBJ_FROM_PTR(self), NULL);
	}

	// Re-enable interrupt ONLY for edge types
	if ((self->irq_type != GPIO_INTR_LOW_LEVEL) && (self->irq_type != GPIO_INTR_HIGH_LEVEL)) {
		_pin_enable_irq(self);
	}

	vTaskDelete(NULL);
}

//--------------------------------------------
STATIC void machine_pin_isr_handler(void *arg)
{
	// Disable gpio interrupt
	gpio_set_intr_type(((machine_pin_obj_t *)arg)->id, GPIO_PIN_INTR_DISABLE);
    machine_pin_obj_t *self = (machine_pin_obj_t *)arg;

    if (self->irq_debounce > 0) {
        // Start the debounce task
        #if CONFIG_MICROPY_USE_BOTH_CORES
        xTaskCreate(debounce_task, "pin_task", 768, (void *)arg, CONFIG_MICROPY_TASK_PRIORITY, NULL);
        #else
        xTaskCreatePinnedToCore(debounce_task, "pin_task", 768, (void *)arg, CONFIG_MICROPY_TASK_PRIORITY, NULL, MainTaskCore);
        #endif
    }
    else {
        if (self->irq_handler) {
            // schedule the callback function
            self->irq_retvalue = gpio_get_level(self->id);
            mp_sched_schedule(self->irq_handler, MP_OBJ_FROM_PTR(self), NULL);
        }

        // Re-enable interrupt ONLY for edge types
        if ((self->irq_type != GPIO_INTR_LOW_LEVEL) && (self->irq_type != GPIO_INTR_HIGH_LEVEL)) {
            _pin_enable_irq(self);
        }
    }
}

//----------------------------------------------------------------------------------------------
STATIC void machine_pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pin_obj_t *self = self_in;

    char smode[32];
    if (self->mode == GPIO_MODE_DISABLE) sprintf(smode, "DISABLED");
    else if (self->mode == GPIO_MODE_INPUT) sprintf(smode, "IN");
    else if (self->mode == GPIO_MODE_OUTPUT) sprintf(smode, "OUT");
    else if (self->mode == GPIO_MODE_OUTPUT_OD) sprintf(smode, "OUT_OD");
    else if (self->mode == GPIO_MODE_INPUT_OUTPUT_OD) sprintf(smode, "INOUT_OD");
    else if (self->mode == GPIO_MODE_INPUT_OUTPUT) sprintf(smode, "INOUT");
    else sprintf(smode, "Unknown");

    if (self->pull == GPIO_PULLUP_ONLY) strcat(smode, ", PULL_UP");
    else if (self->pull == GPIO_PULLDOWN_ONLY) strcat(smode, ", PULL_DOWN");
    else if (self->pull == GPIO_PULLUP_PULLDOWN) strcat(smode, ", PULL_UPDOWN");
    else if (self->pull == GPIO_FLOATING) strcat(smode, ", PULL_FLOAT");
    else strcat(smode, ", Unknown");

    mp_printf(print, "Pin(%u) mode=%s", self->id, smode);
    if (self->irq_handler) {
    	uint32_t pin_irq_type = self->irq_type;
        char sirq[32];
        if (pin_irq_type == GPIO_INTR_DISABLE) sprintf(sirq, "IRQ_DISABLED");
        else if (pin_irq_type == GPIO_INTR_POSEDGE) sprintf(sirq, "IRQ_RISING");
        else if (pin_irq_type == GPIO_INTR_NEGEDGE) sprintf(sirq, "IRQ_FALLING");
        else if (pin_irq_type == GPIO_INTR_ANYEDGE) {
            uint32_t pin_irq_type_any = GPIO.pin[self->id].int_type;
            char sirq_any[8];
            if (pin_irq_type_any == GPIO_INTR_LOW_LEVEL) sprintf(sirq_any, "LOLEVEL");
            else if (pin_irq_type_any == GPIO_INTR_HIGH_LEVEL) sprintf(sirq_any, "HILEVEL");
            else sprintf(sirq_any, "?");
            sprintf(sirq, "IRQ_ANYEDGE (%s)", sirq_any);
        }
        else if (pin_irq_type == GPIO_INTR_LOW_LEVEL) sprintf(sirq, "IRQ_LOLEVEL");
        else if (pin_irq_type == GPIO_INTR_HIGH_LEVEL) sprintf(sirq, "IRQ_HILEVEL");
        else sprintf(sirq, "Unknown");
    	mp_printf(print, ", irq=%s, debounce=%u, actTime=%d", sirq, self->irq_debounce, self->irq_active_time);
    }
}

// constructor(id, ...)
//-------------------------------------------------------------------------------------------------------
mp_obj_t mp_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	enum { ARG_pin, ARG_mode, ARG_pull, ARG_value, ARG_handler, ARG_trigger, ARG_debounce, ARG_acttime };
	static const mp_arg_t mp_pin_allowed_args[] = {
	    { MP_QSTR_pin,						 MP_ARG_INT, {.u_int = 40}},
	    { MP_QSTR_mode,						 MP_ARG_OBJ, {.u_obj = mp_const_none}},
	    { MP_QSTR_pull,						 MP_ARG_OBJ, {.u_obj = mp_const_none}},
	    { MP_QSTR_value,	MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
	    { MP_QSTR_handler,	MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
	    { MP_QSTR_trigger,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = GPIO_PIN_INTR_DISABLE} },
	    { MP_QSTR_debounce,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
	    { MP_QSTR_acttime,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
	};

	mp_arg_val_t args[MP_ARRAY_SIZE(mp_pin_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(mp_pin_allowed_args), mp_pin_allowed_args, args);

    // get the wanted pin object
    int wanted_pin = args[ARG_pin].u_int;

    if (!GPIO_IS_VALID_GPIO(wanted_pin)) {
        mp_raise_ValueError("invalid pin");
    }
	if (mpy_use_spiram) {
		if ((wanted_pin == 16) || (wanted_pin == 17)) {
			mp_raise_ValueError("Pins 16&17 cannot be used if SPIRAM is used");
		}
	}

	// Create Pin object
    //machine_pin_obj_t *self = m_new_obj(machine_pin_obj_t);
    machine_pin_obj_t *self = m_new_obj_with_finaliser(machine_pin_obj_t);
    memset(self, 0, sizeof(machine_pin_obj_t));
    self->base.type = &machine_pin_type;
    self->id = wanted_pin;
    self->mode = GPIO_MODE_INPUT;
    self->pull = GPIO_FLOATING;

    gpio_config_t pin_conf;
    pin_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    pin_conf.mode = GPIO_MODE_INPUT;
    pin_conf.pin_bit_mask = (1ULL<<wanted_pin);
    pin_conf.pull_down_en = 0;
    pin_conf.pull_up_en = 0;

    // configure mode
    if (args[ARG_mode].u_obj != mp_const_none) {
    	int pin_io_mode = -1;
        int in_pin_mode = mp_obj_get_int(args[ARG_mode].u_obj);
        for (int i=0; i<sizeof(pin_modes); i++) {
        	if (in_pin_mode == pin_modes[i]) {
        		pin_io_mode = in_pin_mode;
        		break;
        	}
        }
        if (pin_io_mode < 0) {
            mp_raise_ValueError("unsupported pin mode");
        }
        if (pin_io_mode & GPIO_MODE_DEF_OUTPUT) {
        	// output
        	if (!GPIO_IS_VALID_OUTPUT_GPIO(self->id)) {
                mp_raise_ValueError("not a valid output pin");
        	}
        }
        pin_conf.mode = pin_io_mode;
        self->mode = pin_io_mode;
    }

    // configure pull
    if (args[ARG_pull].u_obj != mp_const_none) {
        if (self->id >= 34) {
        	mp_raise_ValueError("pins 34~39 do not have pull-up or pull-down circuitry");
        }
    	int pin_pull = -1;
        int in_pin_pull_mode = mp_obj_get_int(args[ARG_pull].u_obj);
        for (int i=0; i<sizeof(pin_pull_modes); i++) {
        	if (in_pin_pull_mode == pin_pull_modes[i]) {
        		pin_pull = in_pin_pull_mode;
        		break;
        	}
        }
        if (pin_pull < 0) {
            mp_raise_ValueError("unsupported pull mode");
        }
        if (pin_pull == GPIO_PULLUP_ONLY) pin_conf.pull_up_en = 1;
        else if (pin_pull == GPIO_PULLDOWN_ONLY) pin_conf.pull_down_en = 1;
        else if (pin_pull == GPIO_PULLUP_PULLDOWN) {
			pin_conf.pull_down_en = 1;
			pin_conf.pull_up_en = 1;
        }
        self->pull = pin_pull;
    }

    // configure the pin for gpio
    if (rtc_gpio_is_valid_gpio(self->id)) rtc_gpio_deinit(self->id);
    gpio_pad_select_gpio(self->id);

    gpio_config(&pin_conf);

    // set initial value
    if (args[ARG_value].u_obj != MP_OBJ_NULL) {
        gpio_set_level(self->id, mp_obj_is_true(args[ARG_value].u_obj));
    }

    _pin_disable_irq(self);
	if ((self->mode != GPIO_MODE_OUTPUT) && (self->mode != GPIO_MODE_OUTPUT_OD)) {
		// Only input modes can have interrupts, configure it
		if (args[ARG_handler].u_obj != mp_const_none) {
            // Disable pin interrupts and remove isr handler
            gpio_set_intr_type(self->id, GPIO_PIN_INTR_DISABLE);
            gpio_isr_handler_remove(self->id);

            // Check arguments
			if ((!MP_OBJ_IS_FUN(args[ARG_handler].u_obj)) && (!MP_OBJ_IS_METH(args[ARG_handler].u_obj))) {
				mp_raise_ValueError("callback function expected");
			}
			if ((args[ARG_trigger].u_int < GPIO_PIN_INTR_DISABLE) || (args[ARG_trigger].u_int >= GPIO_INTR_MAX)) {
				mp_raise_ValueError("invalid trigger type");
			}
			if ((args[ARG_debounce].u_int != 0) && ((args[ARG_debounce].u_int < 100) || (args[ARG_debounce].u_int > 500000))) {
				mp_raise_ValueError("wrong debounce range (0 or 100 - 500000 us)");
			}
            if ((args[ARG_acttime].u_int != 0) && ((args[ARG_acttime].u_int < 100) || (args[ARG_acttime].u_int > 500000))) {
				mp_raise_ValueError("wrong active time range (0 or 100 - 500000 us)");
			}
			self->irq_handler = NULL;
			self->irq_type = (int8_t)args[ARG_trigger].u_int;

			self->irq_debounce = args[ARG_debounce].u_int;
			self->irq_active_time = args[ARG_acttime].u_int;
			if (self->irq_active_time > self->irq_debounce) self->irq_active_time = self->irq_debounce;

			self->irq_handler = args[ARG_handler].u_obj;

            // Add pin ISR handler
			if (gpio_isr_handler_add(self->id, machine_pin_isr_handler, (void*)self) != ESP_OK) {
				self->irq_handler = NULL;
				self->irq_type = GPIO_PIN_INTR_DISABLE;
				self->irq_debounce = 0;
				gpio_set_intr_type(self->id, GPIO_PIN_INTR_DISABLE);
				mp_raise_ValueError("error adding ISR handler");
			}
			// Enable pin interrupt
			_pin_enable_irq(self);
		}
	}

    return MP_OBJ_FROM_PTR(self);
}

// pin.init(mode, pull)
//---------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_pin_obj_init(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_pull, ARG_value, ARG_handler, ARG_trigger, ARG_debounce, ARG_acttime };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode,						 MP_ARG_INT, {.u_int = -1}},
        { MP_QSTR_pull,						 MP_ARG_INT, {.u_int = -1}},
        { MP_QSTR_value,	MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
	    { MP_QSTR_handler,	MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
	    { MP_QSTR_trigger,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
	    { MP_QSTR_debounce,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
	    { MP_QSTR_acttime,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    machine_pin_obj_t *self = pos_args[0];

    // parse arguments
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (self->irq_handler) {
    	// Disable pin interrupt
        gpio_set_intr_type(self->id, GPIO_PIN_INTR_DISABLE);
    }

    // set initial value (do this before configuring mode/pull)
    if (args[ARG_value].u_obj != MP_OBJ_NULL) {
        gpio_set_level(self->id, mp_obj_is_true(args[ARG_value].u_obj));
    }

    // configure mode
    if (args[ARG_mode].u_int >= 0) {
    	int pin_io_mode = -1;
        for (int i=0; i<sizeof(pin_modes); i++) {
        	if (args[ARG_mode].u_int == pin_modes[i]) {
        		pin_io_mode = args[ARG_mode].u_int;
        		break;
        	}
        }
        if (pin_io_mode < 0) {
            if (self->irq_handler) _pin_enable_irq(self);
            mp_raise_ValueError("unsupported pin mode");
        }
        if (self->mode != pin_io_mode) {
			if (pin_io_mode & GPIO_MODE_DEF_OUTPUT) {
				// output
				if (!GPIO_IS_VALID_OUTPUT_GPIO(self->id)) {
		            if (self->irq_handler) _pin_enable_irq(self);
					mp_raise_ValueError("not a valid output pin");
				}
			}
			if (gpio_set_direction(self->id, pin_io_mode) != ESP_OK) {
				gpio_set_direction(self->id, self->mode);
			}
			else self->mode = pin_io_mode;
			if ((pin_io_mode == GPIO_MODE_OUTPUT) || (pin_io_mode == GPIO_MODE_OUTPUT_OD)) {
				// no interrupts for OUTPUT modes
			    _pin_disable_irq(self);
			}
        }
    }

    // configure pull
    if (args[ARG_pull].u_int >= 0) {
        if (self->id >= 34) {
            if (self->irq_handler) _pin_enable_irq(self);;
        	mp_raise_ValueError("pins 34~39 do not have pull-up or pull-down circuitry");
        }
    	int pin_pull = -1;
        for (int i=0; i<sizeof(pin_pull_modes); i++) {
        	if (args[ARG_pull].u_int == pin_pull_modes[i]) {
        		pin_pull = args[ARG_pull].u_int;
        		break;
        	}
        }
        if (pin_pull < 0) {
            if (self->irq_handler) _pin_enable_irq(self);;
            mp_raise_ValueError("unsupported pull mode");
        }
        if (self->pull != pin_pull) {
			if (gpio_set_pull_mode(self->id, pin_pull) != ESP_OK) {
				gpio_set_pull_mode(self->id, self->pull);
			}
			else self->pull = pin_pull;
        }
    }

	if ((self->mode != GPIO_MODE_OUTPUT) && (self->mode != GPIO_MODE_OUTPUT_OD)) {
		// Configure interrupt for input pin modes
		if (args[ARG_handler].u_obj != MP_OBJ_NULL) {
			if (args[ARG_handler].u_obj == mp_const_none) {
			    _pin_disable_irq(self);
			}
			else {
				gpio_isr_handler_remove(self->id);
				if ((!MP_OBJ_IS_FUN(args[ARG_handler].u_obj)) && (!MP_OBJ_IS_METH(args[ARG_handler].u_obj))) {
		            if (self->irq_handler) _pin_enable_irq(self);
					mp_raise_ValueError("callback function expected");
				}
				if (gpio_isr_handler_add(self->id, machine_pin_isr_handler, (void*)self) != ESP_OK) {
				    _pin_disable_irq(self);
					mp_raise_ValueError("error adding ISR handler, IRQ disabled");
				}
				self->irq_handler = args[ARG_handler].u_obj;
			}
		}

		if (self->irq_handler) {
			// set debounce
			if ((args[ARG_debounce].u_int == 0) || ((args[ARG_debounce].u_int >= 100) && (args[ARG_debounce].u_int <= 500000))) {
				self->irq_debounce = args[ARG_debounce].u_int;
			}
			else if (args[ARG_debounce].u_int > 0) {
			    _pin_disable_irq(self);
				mp_raise_ValueError("out of debounce range (100 - 500000 us)");
			}

			// set active time
			if ((args[ARG_acttime].u_int == 0) || ((args[ARG_acttime].u_int >= 100) && (args[ARG_acttime].u_int <= 500000))) {
				self->irq_active_time = args[ARG_acttime].u_int;
			}
			else if (args[ARG_acttime].u_int > 0) {
			    _pin_disable_irq(self);
				mp_raise_ValueError("out of active time range (100 - 500000 us)");
			}
			if (self->irq_active_time > self->irq_debounce) self->irq_active_time = self->irq_debounce;

			// set trigger type
			if (args[ARG_trigger].u_int >= 0) {
				if (self->irq_type != (int8_t)args[ARG_trigger].u_int) {
					if ((args[ARG_trigger].u_int < GPIO_PIN_INTR_DISABLE) || (args[ARG_trigger].u_int >= GPIO_INTR_MAX)) {
					    _pin_disable_irq(self);
						mp_raise_ValueError("invalid trigger type");
					}
					self->irq_type = (int8_t)args[ARG_trigger].u_int;
		            // Enable interrupts
		            _pin_enable_irq(self);
				}
			}

			self->irq_retvalue = -1;
		}
	}

	if (self->irq_handler) _pin_enable_irq(self);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_init_obj, 1, machine_pin_obj_init);


// fast method for getting/setting pin value
//----------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    machine_pin_obj_t *self = self_in;
    if (n_args == 0) {
        // get pin
        return MP_OBJ_NEW_SMALL_INT(gpio_get_level(self->id));
    } else {
        // set pin
        gpio_set_level(self->id, mp_obj_is_true(args[0]));
        return mp_const_none;
    }
}

// pin.value([value])
//----------------------------------------------------------------------
STATIC mp_obj_t machine_pin_value(size_t n_args, const mp_obj_t *args) {
    return machine_pin_call(args[0], n_args - 1, 0, args + 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_value_obj, 1, 2, machine_pin_value);

//-------------------------------------------------------
STATIC mp_obj_t machine_pin_irq_value(mp_obj_t self_in) {
    machine_pin_obj_t *self = self_in;

    return MP_OBJ_NEW_SMALL_INT(self->irq_retvalue);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_irq_value_obj, machine_pin_irq_value);

//----------------------------------------------------
STATIC mp_obj_t machine_pin_deinit(mp_obj_t self_in) {
    machine_pin_obj_t *self = self_in;

    if (self->irq_handler) {
        gpio_set_intr_type(self->id, GPIO_PIN_INTR_DISABLE);
		gpio_isr_handler_remove(self->id);
        self->irq_handler = NULL;
        self->irq_type = GPIO_PIN_INTR_DISABLE;
        self->irq_debounce = 0;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_deinit_obj, machine_pin_deinit);


//================================================================
STATIC const mp_rom_map_elem_t machine_pin_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR___del__),		MP_ROM_PTR(&machine_pin_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_init),		MP_ROM_PTR(&machine_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),		MP_ROM_PTR(&machine_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_irqvalue),	MP_ROM_PTR(&machine_pin_irq_value_obj) },

    // class constants
    { MP_ROM_QSTR(MP_QSTR_IN),			MP_ROM_INT(GPIO_MODE_INPUT) },
    { MP_ROM_QSTR(MP_QSTR_OUT),			MP_ROM_INT(GPIO_MODE_OUTPUT) },
    { MP_ROM_QSTR(MP_QSTR_OUT_OD),		MP_ROM_INT(GPIO_MODE_OUTPUT_OD) },
    { MP_ROM_QSTR(MP_QSTR_INOUT),		MP_ROM_INT(GPIO_MODE_INPUT_OUTPUT) },
    { MP_ROM_QSTR(MP_QSTR_INOUT_OD),	MP_ROM_INT(GPIO_MODE_INPUT_OUTPUT_OD) },

	{ MP_ROM_QSTR(MP_QSTR_PULL_UP),		MP_ROM_INT(GPIO_PULLUP_ONLY) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DOWN),	MP_ROM_INT(GPIO_PULLDOWN_ONLY) },
    { MP_ROM_QSTR(MP_QSTR_PULL_UPDOWN),	MP_ROM_INT(GPIO_PULLUP_PULLDOWN) },
    { MP_ROM_QSTR(MP_QSTR_PULL_FLOAT),	MP_ROM_INT(GPIO_FLOATING) },

	{ MP_ROM_QSTR(MP_QSTR_IRQ_DISABLE),	MP_ROM_INT(GPIO_PIN_INTR_DISABLE) },
	{ MP_ROM_QSTR(MP_QSTR_IRQ_RISING),	MP_ROM_INT(GPIO_PIN_INTR_POSEDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING),	MP_ROM_INT(GPIO_PIN_INTR_NEGEDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_ANYEDGE),	MP_ROM_INT(GPIO_PIN_INTR_ANYEDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_LOLEVEL),	MP_ROM_INT(GPIO_PIN_INTR_LOLEVEL) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_HILEVEL),	MP_ROM_INT(GPIO_PIN_INTR_HILEVEL) },
};

//--------------------------------------------------------------------------------------------
STATIC mp_uint_t pin_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    (void)errcode;
    machine_pin_obj_t *self = self_in;

    switch (request) {
        case MP_PIN_READ: {
            return gpio_get_level(self->id);
        }
        case MP_PIN_WRITE: {
            gpio_set_level(self->id, arg);
            return 0;
        }
    }
    return -1;
}

STATIC MP_DEFINE_CONST_DICT(machine_pin_locals_dict, machine_pin_locals_dict_table);

//-----------------------------------
STATIC const mp_pin_p_t pin_pin_p = {
  .ioctl = pin_ioctl,
};

//======================================
const mp_obj_type_t machine_pin_type = {
    { &mp_type_type },
    .name = MP_QSTR_Pin,
    .print = machine_pin_print,
    .make_new = mp_pin_make_new,
    .call = machine_pin_call,
    .protocol = &pin_pin_p,
    .locals_dict = (mp_obj_t)&machine_pin_locals_dict,
};
