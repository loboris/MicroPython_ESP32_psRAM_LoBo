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
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "modmachine.h"

#define UART_CB_TYPE_DATA		1
#define UART_CB_TYPE_PATTERN	2
#define UART_CB_TYPE_ERROR		3
#define UART_BUFF_SIZE			256

typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uart_port_t uart_num;
    int8_t bits;
    int8_t parity;
    int8_t stop;
    int8_t tx;
    int8_t rx;
    int8_t rts;
    int8_t cts;
    int data_cb_size;
    uint8_t pattern[16];
    uint8_t pattern_len;
    uint16_t timeout;       // timeout waiting for first char (in ms)
    uint16_t buffer_size;
    uint32_t *data_cb;
    uint32_t *pattern_cb;
    uint32_t *error_cb;
    uint8_t end_task;
    uint8_t lineend[3];
} machine_uart_obj_t;

typedef struct _uart_ringbuf_t {
    uint8_t *buf;
    uint16_t size;
    uint16_t iget;
    uint16_t iput;
} uart_ringbuf_t;

STATIC const char *_parity_name[] = {"None", "1", "0"};
static QueueHandle_t UART_QUEUE[2] = {NULL};
static QueueHandle_t uart_mutex = NULL;
TaskHandle_t task_id[2] = {NULL};

static uart_ringbuf_t uart_buffer[2];
static uart_ringbuf_t *uart_buf[2] = {NULL};

//-----------------------------------------------------------
static void uart_ringbuf_alloc(uint8_t uart_num, uint16_t sz)
{
	uart_buffer[uart_num].buf = m_new(uint8_t, sz);
	uart_buffer[uart_num].size = sz;
	uart_buffer[uart_num].iget = 0;
	uart_buffer[uart_num].iput = 0;
	uart_buf[uart_num] = &uart_buffer[uart_num];
}

//-----------------------------------------------------------------------
static int uart_buf_get(uart_ringbuf_t *r, uint8_t *dest, uint16_t len) {
    if (r->iget == r->iput) return -1; // input buffer empty

    int res = 0;
	for (int i=0; i<len; i++) {
		dest[i] = r->buf[r->iget++];
		res++;
	    if (r->iget == r->iput) break;
	}
	// move the buffer and adjust the pointers
	memmove(r->buf, r->buf+res, res);
	r->iget -= res;
	r->iput -= res;

    return res;
}

//-------------------------------------------------------------------------
static int uart_buf_put(uart_ringbuf_t *r, uint8_t *source, uint16_t len) {
	int res = 0;
	for (int i=0; i<len; i++) {
	    if (r->iput >= r->size) return 1; // overflow
	    r->buf[r->iput++] = source[i];
	}
	return res;
}

//---------------------------------------------------------------------------------------
int pattern_match(uint8_t *text, int text_length, uint8_t *pattern, int pattern_length) {
	int c, d, e, position = -1;

	if (pattern_length > text_length) return -1;

	for (c = 0; c <= (text_length - pattern_length); c++) {
		position = e = c;

		for (d = 0; d < pattern_length; d++) {
			if (pattern[d] == text[e]) e++;
			else break;
		}
		if (d == pattern_length) return position;
	}

	return -1;
}

//--------------------------------------------------------------------------------------------
static void _sched_callback(mp_obj_t function, int uart, int type, int iarglen, uint8_t *sarg)
{
	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
	if (carg == NULL) return;
	if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_INT, uart, NULL, NULL)) return;
	if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_INT, type, NULL, NULL)) return;
	if (sarg) {
		if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_INT, iarglen, NULL, NULL)) return;
	}
	else {
		if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, iarglen, sarg, NULL)) return;
	}
	mp_sched_schedule(function, mp_const_none, carg);
}

//---------------------------------------------
static void uart_event_task(void *pvParameters)
{
	machine_uart_obj_t *self = (machine_uart_obj_t *)pvParameters;
    uart_event_t event;
    size_t datasize;
    int res;
    uint8_t* dtmp = (uint8_t*) malloc(UART_BUFF_SIZE);

    for(;;) {
    	if (self->end_task) break;
    	if (UART_QUEUE[self->uart_num] == NULL) {
    		vTaskDelay(1000 / portTICK_PERIOD_MS);
    		continue;
    	}
        //Waiting for UART event.
        if (xQueueReceive(UART_QUEUE[self->uart_num], (void * )&event, 1000 / portTICK_PERIOD_MS)) {
        	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
            bzero(dtmp, UART_BUFF_SIZE);
            switch(event.type) {
                //Event of UART receiving data
                case UART_DATA:
                	// move UART data to MPy buffer
                    uart_get_buffered_data_len(self->uart_num+1, &datasize);
                    if (datasize > 0) {
                    	// read data from UART buffer
						if (uart_read_bytes(self->uart_num+1, dtmp, datasize, 0) > 0) {
							res = uart_buf_put(uart_buf[self->uart_num], dtmp, datasize);
							if (res) {
								// MPy buffer full
								if (self->error_cb) {
									_sched_callback(self->error_cb, self->uart_num+1, UART_CB_TYPE_ERROR, UART_BUFFER_FULL, NULL);
								}
							}
							else {
								if ((self->data_cb) && (self->data_cb_size > 0) && (uart_buf[self->uart_num]->iput >= self->data_cb_size)) {
									// ** callback on data length received
									uart_buf_get(uart_buf[self->uart_num], dtmp, self->data_cb_size);
									_sched_callback(self->data_cb, self->uart_num+1, UART_CB_TYPE_DATA, self->data_cb_size, dtmp);
								}
								else if (self->pattern_cb) {
									// ** callback on pattern received
									res = pattern_match(uart_buf[self->uart_num]->buf, uart_buf[self->uart_num]->iput, self->pattern, self->pattern_len);
									if (res >= 0) {
										// found, pull data, including pattern from buffer
										uart_buf_get(uart_buf[self->uart_num], dtmp, res+self->pattern_len);
										_sched_callback(self->pattern_cb, self->uart_num+1, UART_CB_TYPE_PATTERN, res, dtmp);
									}
								}
							}
						}
                    }
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(self->uart_num+1);
                    xQueueReset(UART_QUEUE[self->uart_num]);
                    if (self->error_cb) {
						_sched_callback(self->error_cb, self->uart_num+1, UART_CB_TYPE_ERROR, UART_FIFO_OVF, NULL);
                    }
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    // If buffer full happened, you should consider increasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(self->uart_num+1);
                    xQueueReset(UART_QUEUE[self->uart_num]);
                    if (self->error_cb) {
						_sched_callback(self->error_cb, self->uart_num+1, UART_CB_TYPE_ERROR, UART_BUFFER_FULL, NULL);
                    }
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    if (self->error_cb) {
						_sched_callback(self->error_cb, self->uart_num+1, UART_CB_TYPE_ERROR, UART_BREAK, NULL);
                    }
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    if (self->error_cb) {
						_sched_callback(self->error_cb, self->uart_num+1, UART_CB_TYPE_ERROR, UART_PARITY_ERR, NULL);
                    }
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    if (self->error_cb) {
						_sched_callback(self->error_cb, self->uart_num+1, UART_CB_TYPE_ERROR, UART_FRAME_ERR, NULL);
                    }
                    break;
                //Others
                default:
                    //ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }

        	if (uart_mutex) xSemaphoreGive(uart_mutex);
        }
    }
    free(dtmp);
    dtmp = NULL;
    task_id[self->uart_num] = NULL;
    vTaskDelete(NULL);
}


/******************************************************************************/
// MicroPython bindings for UART

//--------------------------------------
static const mp_arg_t allowed_args[] = {
    { MP_QSTR_baudrate,						 MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_bits,							 MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_parity,						 MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_stop,							 MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_tx,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_rx,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_rts,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_cts,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_timeout,		MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_buffer_size,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 512} },
    { MP_QSTR_lineend,		MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
};

enum { ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_tx, ARG_rx, ARG_rts, ARG_cts, ARG_timeout, ARG_buffer_size, ARG_lineend };

//-----------------------------------------------------------------------------------------------
STATIC void machine_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t baudrate;
    uart_get_baudrate(self->uart_num+1, &baudrate);

    mp_printf(print, "UART(%u, baudrate=%u, bits=%u, parity=%s, stop=%u, tx=%d, rx=%d, rts=%d, cts=%d, timeout=%u, buf_size=%u)",
        self->uart_num+1, baudrate, self->bits, _parity_name[self->parity],
        self->stop, self->tx, self->rx, self->rts, self->cts, self->timeout, self->buffer_size);
    if (self->data_cb) {
    	mp_printf(print, "\n     data CB: True, on len: %d", self->data_cb_size);
    }
    if (self->pattern_cb) {
    	mp_printf(print, "\n     pattern CB: True, pattern: [%s]", self->pattern);
    }
    if (self->error_cb) {
    	mp_printf(print, "\n     error CB: True");
    }
    if (task_id[self->uart_num]) {
    	mp_printf(print, "\n     Event task minimum free stack: %u", uxTaskGetStackHighWaterMark(task_id[self->uart_num]));
    }
}

//--------------------------------------------------------------------------------------------------------------------------
STATIC void machine_uart_init_helper(machine_uart_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // wait for all data to be transmitted before changing settings
    uart_wait_tx_done(self->uart_num+1, pdMS_TO_TICKS(1000));

    // set baudrate
    uint32_t baudrate = 115200;
    if (args[ARG_baudrate].u_int > 0) {
        uart_set_baudrate(self->uart_num+1, args[ARG_baudrate].u_int);
        uart_get_baudrate(self->uart_num+1, &baudrate);
    }

    // set data bits
    if ((args[ARG_bits].u_int >= 0) && (args[ARG_bits].u_int != self->bits)) {
		switch (args[ARG_bits].u_int) {
			case 0:
				break;
			case 5:
				uart_set_word_length(self->uart_num+1, UART_DATA_5_BITS);
				self->bits = 5;
				break;
			case 6:
				uart_set_word_length(self->uart_num+1, UART_DATA_6_BITS);
				self->bits = 6;
				break;
			case 7:
				uart_set_word_length(self->uart_num+1, UART_DATA_7_BITS);
				self->bits = 7;
				break;
			case 8:
				uart_set_word_length(self->uart_num+1, UART_DATA_8_BITS);
				self->bits = 8;
				break;
			default:
				mp_raise_ValueError("invalid data bits");
				break;
		}
    }

    // set parity
    if (args[ARG_parity].u_obj != MP_OBJ_NULL) {
        if (args[ARG_parity].u_obj == mp_const_none) {
        	if (self->parity != UART_PARITY_DISABLE) {
                uart_set_parity(self->uart_num+1, UART_PARITY_DISABLE);
                self->parity = UART_PARITY_DISABLE;
        }
        }
        else {
            mp_int_t parity = mp_obj_get_int(args[ARG_parity].u_obj);
            if ((parity & 1) && (self->parity != UART_PARITY_ODD)) {
                uart_set_parity(self->uart_num+1, UART_PARITY_ODD);
                self->parity = UART_PARITY_ODD;
            }
            else if (self->parity != UART_PARITY_EVEN){
                uart_set_parity(self->uart_num+1, UART_PARITY_EVEN);
                self->parity = UART_PARITY_EVEN;
            }
        }
    }

    // set stop bits
    if ((args[ARG_stop].u_int >= 0) && (args[ARG_stop].u_int != self->stop)) {
		switch (args[ARG_stop].u_int) {
			case 0:
				break;
			case 1:
				uart_set_stop_bits(self->uart_num+1, UART_STOP_BITS_1);
				self->stop = UART_STOP_BITS_1;
				break;
			case 2:
				uart_set_stop_bits(self->uart_num+1, UART_STOP_BITS_2);
				self->stop = UART_STOP_BITS_2;
				break;
			case 3:
				uart_set_stop_bits(self->uart_num+1, UART_STOP_BITS_1_5);
				self->stop = UART_STOP_BITS_1_5;
				break;
			default:
				mp_raise_ValueError("invalid stop bits");
				break;
		}
    }

    // set pins
    if (((self->tx == -2) && (args[ARG_tx].u_int == UART_PIN_NO_CHANGE)) ||	((self->rx == -2) && (args[ARG_rx].u_int == UART_PIN_NO_CHANGE))) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Tx&Rx pins must be set: u=machine.UART(uart_num, tx=pin, rx=pin)"));
    }

    if ((self->tx != args[ARG_tx].u_int) || (self->rx != args[ARG_rx].u_int) ||
    	(self->rts != args[ARG_rts].u_int) || (self->cts != args[ARG_cts].u_int)) {

    	esp_err_t res = uart_set_pin(self->uart_num+1, args[ARG_tx].u_int, args[ARG_rx].u_int, args[ARG_rts].u_int, args[ARG_cts].u_int);
		if (res != ESP_OK) {
			nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Error setting pins"));
		}

		if (args[ARG_tx].u_int != UART_PIN_NO_CHANGE)  self->tx  = args[ARG_tx].u_int;
		if (args[ARG_rx].u_int != UART_PIN_NO_CHANGE)  self->rx  = args[ARG_rx].u_int;
		if (args[ARG_rts].u_int != UART_PIN_NO_CHANGE) self->rts = args[ARG_rts].u_int;
		if (args[ARG_cts].u_int != UART_PIN_NO_CHANGE) self->cts = args[ARG_cts].u_int;
    }

    // set timeout
    if (args[ARG_timeout].u_int >= 0) self->timeout = args[ARG_timeout].u_int;

    // set line end
    if (MP_OBJ_IS_STR(args[ARG_lineend].u_obj)) {
    	size_t lnendlen;
    	const char *lnend = mp_obj_str_get_data(args[ARG_lineend].u_obj, &lnendlen);
    	if ((lnend) && (lnendlen > 0) && (lnendlen > 0)) sprintf((char *)self->lineend, "%s", lnend);
	}
}

//------------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_uart_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // get uart id
    mp_int_t uart_num = mp_obj_get_int(args[0]);
    if (uart_num < 0 || uart_num >= UART_NUM_MAX) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "UART(%d) does not exist", uart_num));
    }

    // Attempts to use UART0 from Python has resulted in all sorts of fun errors.
    // FIXME: UART0 is disabled for now.
    if (uart_num == UART_NUM_0) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "UART(%d) is disabled (dedicated to REPL)", uart_num));
    }

     // Defaults
    uart_config_t uartcfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0
    };

	if (uart_mutex == NULL) {
		uart_mutex = xSemaphoreCreateMutex();
	}

	// Create UART instance, set defaults
    machine_uart_obj_t *self = m_new_obj(machine_uart_obj_t);
    self->base.type = &machine_uart_type;
    self->uart_num = uart_num-1;
    self->bits = UART_DATA_8_BITS;
    self->parity = UART_PARITY_DISABLE;
    self->stop = UART_STOP_BITS_1;
    self->rts = UART_PIN_NO_CHANGE;
    self->cts = UART_PIN_NO_CHANGE;
    self->timeout = 0;
    self->pattern[0] = 0;
    self->pattern_len = 0;
    self->data_cb = NULL;
    self->pattern_cb = NULL;
    self->error_cb = NULL;
    self->data_cb_size = 0;
    self->end_task = 0;
    sprintf((char *)self->lineend, "\r\n");


    switch (uart_num) {
        case UART_NUM_0:
            self->rx = UART_PIN_NO_CHANGE;
            self->tx = UART_PIN_NO_CHANGE;
            break;
        case UART_NUM_1:
            self->rx = -2;
            self->tx = -2;
            break;
        case UART_NUM_2:
            self->rx = -2;
            self->tx = -2;
            break;
    }

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);

    mp_arg_val_t kargs[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, args+1, &kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, kargs);

    int bufsize = kargs[ARG_buffer_size].u_int;
    if (bufsize < 512) bufsize = 512;
    if (bufsize > 8192) bufsize = 8192;
    self->buffer_size = bufsize;

	if (uart_buf[self->uart_num] == NULL) {
		uart_ringbuf_alloc(self->uart_num, bufsize);
		if (uart_buf[self->uart_num] == NULL) {
	        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "UART(%d) Error allocating ring buffer", uart_num));
		}
	}

	// Remove any existing configuration
    uart_driver_delete(uart_num);

    // init the peripheral
    // Setup
    uart_param_config(uart_num, &uartcfg);

    // RX ring buffer size is set to UART_BUFF_SIZE (256), TX buffer is disabled.
    esp_err_t res = uart_driver_install(uart_num, UART_BUFF_SIZE, 0, 10, &UART_QUEUE[self->uart_num], 0);
    if (res != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "UART(%d) Error installing driver", uart_num));
    }

    machine_uart_init_helper(self, n_args - 1, args + 1, &kw_args);

    // Make sure pins are connected.
    uart_set_pin(uart_num, self->tx, self->rx, self->rts, self->cts);

    //Disable uart pattern detect function
    uart_disable_pattern_det_intr(uart_num);

    //Create a task to handle UART event from ISR
    if (task_id[self->uart_num] == NULL) xTaskCreate(uart_event_task, "uart_event_task", 1024, (void *)self, CONFIG_MICROPY_TASK_PRIORITY+4, &task_id[self->uart_num]);

    return MP_OBJ_FROM_PTR(self);
}

//-----------------------------------------------------------------------------------------
STATIC mp_obj_t machine_uart_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    machine_uart_init_helper(args[0], n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_uart_init_obj, 1, machine_uart_init);

//--------------------------------------------------
STATIC mp_obj_t machine_uart_any(mp_obj_t self_in) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
    int res = uart_buf[self->uart_num]->iput;
	if (uart_mutex) xSemaphoreGive(uart_mutex);

    return MP_OBJ_NEW_SMALL_INT(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_any_obj, machine_uart_any);

//-----------------------------------------------------
STATIC mp_obj_t machine_uart_flush(mp_obj_t self_in) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
    uart_flush_input(self->uart_num+1);
    uart_buf[self->uart_num]->iput = 0;
    uart_buf[self->uart_num]->iget = 0;
	if (uart_mutex) xSemaphoreGive(uart_mutex);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_flush_obj, machine_uart_flush);

//------------------------------------------------------------------------
STATIC mp_obj_t machine_uart_readln(size_t n_args, const mp_obj_t *args) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    vstr_t vstr;
    int res = -1;
	int lnendlen = strlen((char *)self->lineend);
	int timeout = self->timeout;
	if (n_args == 2) timeout = mp_obj_get_int(args[1]);

	if (timeout == 0) {
		if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
    	// just return the buffer content if line end was found
		if (uart_buf[self->uart_num]->iput < lnendlen) {
	    	if (uart_mutex) xSemaphoreGive(uart_mutex);
	    	return mp_const_none;
		}
		res = pattern_match(uart_buf[self->uart_num]->buf, uart_buf[self->uart_num]->iput, self->lineend, lnendlen);
		if (res >= 0) {
			// found, pull data, including pattern from buffer
			vstr_init_len(&vstr, res+lnendlen);
			uart_buf_get(uart_buf[self->uart_num], (uint8_t *)vstr.buf, res+lnendlen);
		}
    	if (uart_mutex) xSemaphoreGive(uart_mutex);
    	if (res < 0) return mp_const_none;
    }
    else {
    	// wait until line end received or timeout
    	int wait = timeout;
        int buflen = 0;
    	mp_hal_set_wdt_tmo();
		MP_THREAD_GIL_EXIT();
		while (wait > 0) {
			if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
			if (buflen < uart_buf[self->uart_num]->iput) {
				buflen = uart_buf[self->uart_num]->iput;
				wait = timeout; // new data received, reset timeout
			}
			if (uart_buf[self->uart_num]->iput < lnendlen) {
		    	if (uart_mutex) xSemaphoreGive(uart_mutex);
	    		vTaskDelay(10 / portTICK_PERIOD_MS);
				wait -= 10;
				mp_hal_reset_wdt();
				continue;
			}
			res = pattern_match(uart_buf[self->uart_num]->buf, uart_buf[self->uart_num]->iput, self->lineend, lnendlen);
			if (res >= 0) {
				// found, pull data, including pattern from buffer
				vstr_init_len(&vstr, res+lnendlen);
				uart_buf_get(uart_buf[self->uart_num], (uint8_t *)vstr.buf, res+lnendlen);
		    	if (uart_mutex) xSemaphoreGive(uart_mutex);
				break;
			}
	    	if (uart_mutex) xSemaphoreGive(uart_mutex);
    		vTaskDelay(10 / portTICK_PERIOD_MS);
			wait -= 10;
			mp_hal_reset_wdt();
		}
		MP_THREAD_GIL_ENTER();
    	if (res < 0) return mp_const_none;
    }

    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_uart_readln_obj, 1, 2, machine_uart_readln);


//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_uart_callback(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_type, ARG_func, ARG_pattern, ARG_datalen };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_type,			MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_func,			MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_pattern,		MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_data_len,		MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
    };

    machine_uart_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int cbtype = args[ARG_type].u_int;
    if ((!MP_OBJ_IS_FUN(args[ARG_func].u_obj)) && (!MP_OBJ_IS_METH(args[ARG_func].u_obj))) {
    	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
        switch(cbtype) {
            case UART_CB_TYPE_DATA:
        		self->data_cb = NULL;
        		self->data_cb_size = 0;
                break;
            case UART_CB_TYPE_PATTERN:
            	self->pattern_cb = NULL;
            	self->pattern[0] = 0;
            	self->pattern_len = 0;
                break;
            case UART_CB_TYPE_ERROR:
            	self->error_cb = NULL;
                break;
            default:
            	break;
        }
    	if (uart_mutex) xSemaphoreGive(uart_mutex);
        return mp_const_none;
    }


    int datalen = -1;
    size_t patternlen = 0;
    const char * pattern = NULL;

    if (MP_OBJ_IS_STR(args[ARG_pattern].u_obj)) {
    	pattern = mp_obj_str_get_data(args[ARG_pattern].u_obj, &patternlen);
    	if (patternlen > sizeof(self->pattern)) patternlen = sizeof(self->pattern);
	}

    if ((args[ARG_datalen].u_int >= 0) && (args[ARG_datalen].u_int < self->buffer_size)) datalen = args[ARG_datalen].u_int;

	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
    switch(cbtype) {
        case UART_CB_TYPE_DATA:
    		if (datalen >= 0) self->data_cb_size = datalen;
    		self->data_cb = args[ARG_func].u_obj;
            break;
        case UART_CB_TYPE_PATTERN:
        	if (pattern) {
        		memcpy(self->pattern, pattern, patternlen);
        		self->pattern_len = patternlen;
        	}
    		self->pattern_cb = args[ARG_func].u_obj;
            break;
        case UART_CB_TYPE_ERROR:
    		self->error_cb = args[ARG_func].u_obj;
            break;
        default:
        	break;
    }
	if (uart_mutex) xSemaphoreGive(uart_mutex);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_uart_callback_obj, 2, machine_uart_callback);


//=================================================================
STATIC const mp_rom_map_elem_t machine_uart_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),			MP_ROM_PTR(&machine_uart_init_obj) },

    { MP_ROM_QSTR(MP_QSTR_any),				MP_ROM_PTR(&machine_uart_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),			MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline),		MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto),		MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),			MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_readln),			MP_ROM_PTR(&machine_uart_readln_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush),			MP_ROM_PTR(&machine_uart_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_callback),		MP_ROM_PTR(&machine_uart_callback_obj) },

	// class constants
    { MP_ROM_QSTR(MP_QSTR_CBTYPE_DATA),		MP_ROM_INT(UART_CB_TYPE_DATA) },
    { MP_ROM_QSTR(MP_QSTR_CBTYPE_PATTERN),	MP_ROM_INT(UART_CB_TYPE_PATTERN) },
    { MP_ROM_QSTR(MP_QSTR_CBTYPE_ERROR),	MP_ROM_INT(UART_CB_TYPE_ERROR) },
};
STATIC MP_DEFINE_CONST_DICT(machine_uart_locals_dict, machine_uart_locals_dict_table);

//------------------------------------------------------------------------------------------------
STATIC mp_uint_t machine_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // make sure we want at least 1 char
    if (size == 0) {
        return 0;
    }

    int bytes_read = 0;
    if (self->timeout == 0) {
    	// just return the buffer content
    	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
    	bytes_read = uart_buf_get(uart_buf[self->uart_num], (uint8_t *)buf_in, size);
    	if (uart_mutex) xSemaphoreGive(uart_mutex);
    	if (bytes_read < 0) bytes_read = 0;
    }
    else {
    	// wait until data received or timeout
    	mp_hal_set_wdt_tmo();
		int wait = self->timeout;
		MP_THREAD_GIL_EXIT();
		while (wait > 0) {
	    	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
			if (uart_buf[self->uart_num]->iput < size) {
		    	if (uart_mutex) xSemaphoreGive(uart_mutex);
	    		vTaskDelay(2 / portTICK_PERIOD_MS);
				wait -= 2;
				mp_hal_reset_wdt();
				continue;
			}
	    	bytes_read = uart_buf_get(uart_buf[self->uart_num], (uint8_t *)buf_in, size);
	    	if (uart_mutex) xSemaphoreGive(uart_mutex);
			break;
		}
		MP_THREAD_GIL_ENTER();
    }

    if (bytes_read < 0) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }

    return bytes_read;
}

//-------------------------------------------------------------------------------------------------------
STATIC mp_uint_t machine_uart_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);

    int bytes_written = uart_write_bytes(self->uart_num+1, buf_in, size);

    if (bytes_written < 0) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }

    // return number of bytes written
    return bytes_written;
}

//-----------------------------------------------------------------------------------------------------
STATIC mp_uint_t machine_uart_ioctl(mp_obj_t self_in, mp_uint_t request, mp_uint_t arg, int *errcode) {
    machine_uart_obj_t *self = self_in;
    mp_uint_t ret;
    if (request == MP_STREAM_POLL) {
        mp_uint_t flags = arg;
        ret = 0;
        size_t rxbufsize;
    	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
        rxbufsize = uart_buf[self->uart_num]->iput;
    	if (uart_mutex) xSemaphoreGive(uart_mutex);

        if ((flags & MP_STREAM_POLL_RD) && rxbufsize > 0) {
            ret |= MP_STREAM_POLL_RD;
        }
        if ((flags & MP_STREAM_POLL_WR) && 1) { // FIXME: uart_tx_any_room(self->uart_num)
            ret |= MP_STREAM_POLL_WR;
        }
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}

//==========================================
STATIC const mp_stream_p_t uart_stream_p = {
    .read = machine_uart_read,
    .write = machine_uart_write,
    .ioctl = machine_uart_ioctl,
    .is_text = false,
};

//=======================================
const mp_obj_type_t machine_uart_type = {
    { &mp_type_type },
    .name = MP_QSTR_UART,
    .print = machine_uart_print,
    .make_new = machine_uart_make_new,
    .getiter = mp_identity_getiter,
    .iternext = mp_stream_unbuffered_iter,
    .protocol = &uart_stream_p,
    .locals_dict = (mp_obj_dict_t*)&machine_uart_locals_dict,
};
