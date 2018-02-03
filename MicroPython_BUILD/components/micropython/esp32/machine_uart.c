/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
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


typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uart_port_t uart_num;
    uint8_t bits;
    uint8_t parity;
    uint8_t stop;
    int8_t tx;
    int8_t rx;
    int8_t rts;
    int8_t cts;
    uint8_t patern_chr_num;
    int data_cb_size;
    char patern_char;
    uint16_t timeout;       // timeout waiting for first char (in ms)
    uint16_t buffer_size;
    uint32_t *data_cb;
    uint32_t *pattern_cb;
    uint32_t *error_cb;
    TaskHandle_t task_id;
    uint8_t end_task;
} machine_uart_obj_t;

STATIC const char *_parity_name[] = {"None", "1", "0"};
static QueueHandle_t UART_QUEUE[UART_NUM_MAX] = {NULL};
static QueueHandle_t uart_mutex = NULL;


//---------------------------------------------
static void uart_event_task(void *pvParameters)
{
	machine_uart_obj_t *self = (machine_uart_obj_t *)pvParameters;
    uart_event_t event;
    size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(self->buffer_size);
    for(;;) {
    	if (self->end_task) break;
    	if (UART_QUEUE[self->uart_num] == NULL) {
    		vTaskDelay(1000 / portTICK_PERIOD_MS);
    		continue;
    	}
        //Waiting for UART event.
        if (xQueueReceive(UART_QUEUE[self->uart_num], (void * )&event, 1000 / portTICK_PERIOD_MS)) {
        	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
            bzero(dtmp, self->buffer_size);
            switch(event.type) {
                //Event of UART receiving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    if (self->data_cb) {
                        size_t datasize;
                        uart_get_buffered_data_len(self->uart_num, &datasize);
                        if ((self->data_cb_size > 0) && (datasize >= self->data_cb_size)) {
							uart_read_bytes(self->uart_num, dtmp, datasize, 20 / portTICK_PERIOD_MS);
							mp_obj_t tuple[3];
							tuple[0] = mp_obj_new_int(self->uart_num);
							tuple[1] = mp_obj_new_int(UART_CB_TYPE_DATA);
							tuple[2] = mp_obj_new_str((const char*)dtmp, datasize, 0);
							mp_sched_schedule(self->data_cb, mp_obj_new_tuple(3, tuple));
                        }
                    }
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(self->uart_num);
                    xQueueReset(UART_QUEUE[self->uart_num]);
                    if (self->error_cb) {
						mp_obj_t tuple[3];
						tuple[0] = mp_obj_new_int(self->uart_num);
						tuple[1] = mp_obj_new_int(UART_CB_TYPE_ERROR);
						tuple[2] = mp_obj_new_int(UART_FIFO_OVF);
						mp_sched_schedule(self->error_cb, mp_obj_new_tuple(3, tuple));
                    }
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    // If buffer full happened, you should consider increasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(self->uart_num);
                    xQueueReset(UART_QUEUE[self->uart_num]);
                    if (self->error_cb) {
						mp_obj_t tuple[3];
						tuple[0] = mp_obj_new_int(self->uart_num);
						tuple[1] = mp_obj_new_int(UART_CB_TYPE_ERROR);
						tuple[2] = mp_obj_new_int(UART_BUFFER_FULL);
						mp_sched_schedule(self->error_cb, mp_obj_new_tuple(3, tuple));
                    }
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    if (self->error_cb) {
						mp_obj_t tuple[3];
						tuple[0] = mp_obj_new_int(self->uart_num);
						tuple[1] = mp_obj_new_int(UART_CB_TYPE_ERROR);
						tuple[2] = mp_obj_new_int(UART_BREAK);
						mp_sched_schedule(self->error_cb, mp_obj_new_tuple(3, tuple));
                    }
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    if (self->error_cb) {
						mp_obj_t tuple[3];
						tuple[0] = mp_obj_new_int(self->uart_num);
						tuple[1] = mp_obj_new_int(UART_CB_TYPE_ERROR);
						tuple[2] = mp_obj_new_int(UART_PARITY_ERR);
						mp_sched_schedule(self->error_cb, mp_obj_new_tuple(3, tuple));
                    }
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    if (self->error_cb) {
						mp_obj_t tuple[3];
						tuple[0] = mp_obj_new_int(self->uart_num);
						tuple[1] = mp_obj_new_int(UART_CB_TYPE_ERROR);
						tuple[2] = mp_obj_new_int(UART_FRAME_ERR);
						mp_sched_schedule(self->error_cb, mp_obj_new_tuple(3, tuple));
                    }
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(self->uart_num, &buffered_size);
                    int pos = uart_pattern_pop_pos(self->uart_num);
                    if (pos == -1) {
                        // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                        // record the position. We should set a larger queue size.
                        // As an example, we directly flush the rx buffer here.
                        uart_flush_input(self->uart_num);
                    }
                    else {
                        if (self->pattern_cb) {
							uart_read_bytes(self->uart_num, dtmp, pos, 100 / portTICK_PERIOD_MS);
							uint8_t pat[self->patern_chr_num + 1];
							memset(pat, 0, sizeof(pat));
							uart_read_bytes(self->uart_num, pat, self->patern_chr_num, 100 / portTICK_PERIOD_MS);
                        	mp_obj_t tuple[4];
                        	tuple[0] = mp_obj_new_int(self->uart_num);
							tuple[1] = mp_obj_new_int(UART_CB_TYPE_PATTERN);
                        	tuple[2] = mp_obj_new_str((const char*)pat, self->patern_chr_num, 0);
                        	tuple[3] = mp_obj_new_str((const char*)dtmp, pos, 0);
                        	mp_sched_schedule(self->pattern_cb, mp_obj_new_tuple(4, tuple));
                        }
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
    vTaskDelete(NULL);
}


/******************************************************************************/
// MicroPython bindings for UART

//--------------------------------------
static const mp_arg_t allowed_args[] = {
    { MP_QSTR_baudrate,						 MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_bits,							 MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_parity,						 MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_stop,							 MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_tx,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_rx,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_rts,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_cts,			MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_PIN_NO_CHANGE} },
    { MP_QSTR_timeout,		MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_buffer_size,	MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 256} },
};

static enum { ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_tx, ARG_rx, ARG_rts, ARG_cts, ARG_timeout, ARG_buffer_size };

//-----------------------------------------------------------------------------------------------
STATIC void machine_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t baudrate;
    uart_get_baudrate(self->uart_num, &baudrate);

    mp_printf(print, "UART(%u, baudrate=%u, bits=%u, parity=%s, stop=%u, tx=%d, rx=%d, rts=%d, cts=%d, timeout=%u, buf_size=%u)",
        self->uart_num, baudrate, self->bits, _parity_name[self->parity],
        self->stop, self->tx, self->rx, self->rts, self->cts, self->timeout, self->buffer_size);
    if (self->data_cb) {
    	mp_printf(print, "\n     data CB: True, on len: %d", self->data_cb_size);
    }
    if (self->pattern_cb) {
    	mp_printf(print, "\n     patern CB: True, pattern len: %d, pattern char: '%c'", self->patern_chr_num, self->patern_char);
    }
    if (self->error_cb) {
    	mp_printf(print, "\n     error CB: True");
    }
    if (self->task_id) {
    	mp_printf(print, "\n     Event task minimum free stack: %u", uxTaskGetStackHighWaterMark(self->task_id));
    }
}

//--------------------------------------------------------------------------------------------------------------------------
STATIC void machine_uart_init_helper(machine_uart_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // wait for all data to be transmitted before changing settings
    uart_wait_tx_done(self->uart_num, pdMS_TO_TICKS(1000));

    // set baudrate
    uint32_t baudrate = 115200;
    if (args[ARG_baudrate].u_int > 0) {
        uart_set_baudrate(self->uart_num, args[ARG_baudrate].u_int);
        uart_get_baudrate(self->uart_num, &baudrate);
    }

    if (((self->tx == -2) && (args[ARG_tx].u_int == UART_PIN_NO_CHANGE)) ||
    		((self->rx == -2) && (args[ARG_rx].u_int == UART_PIN_NO_CHANGE))) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Tx&Rx pins must be set: u=machine.UART(uart_num, tx=pin, rx=pin)"));
    }

    esp_err_t res =uart_set_pin(self->uart_num, args[ARG_tx].u_int, args[ARG_rx].u_int, args[ARG_rts].u_int, args[ARG_cts].u_int);
    if (res != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Error setting pins"));
    }
    if (args[ARG_tx].u_int != UART_PIN_NO_CHANGE) {
        self->tx = args[ARG_tx].u_int;
    }

    if (args[ARG_rx].u_int != UART_PIN_NO_CHANGE) {
        self->rx = args[ARG_rx].u_int;
    }

    if (args[ARG_rts].u_int != UART_PIN_NO_CHANGE) {
        self->rts = args[ARG_rts].u_int;
    }

    if (args[ARG_cts].u_int != UART_PIN_NO_CHANGE) {
        self->cts = args[ARG_cts].u_int;
    }

    // set data bits
    switch (args[ARG_bits].u_int) {
        case 0:
            break;
        case 5:
            uart_set_word_length(self->uart_num, UART_DATA_5_BITS);
            self->bits = 5;
            break;
        case 6:
            uart_set_word_length(self->uart_num, UART_DATA_6_BITS);
            self->bits = 6;
            break;
        case 7:
            uart_set_word_length(self->uart_num, UART_DATA_7_BITS);
            self->bits = 7;
            break;
        case 8:
            uart_set_word_length(self->uart_num, UART_DATA_8_BITS);
            self->bits = 8;
            break;
        default:
            mp_raise_ValueError("invalid data bits");
            break;
    }

    // set parity
    if (args[ARG_parity].u_obj != MP_OBJ_NULL) {
        if (args[ARG_parity].u_obj == mp_const_none) {
            uart_set_parity(self->uart_num, UART_PARITY_DISABLE);
            self->parity = 0;
        } else {
            mp_int_t parity = mp_obj_get_int(args[ARG_parity].u_obj);
            if (parity & 1) {
                uart_set_parity(self->uart_num, UART_PARITY_ODD);
                self->parity = 1;
            } else {
                uart_set_parity(self->uart_num, UART_PARITY_EVEN);
                self->parity = 2;
            }
        }
    }

    // set stop bits
    switch (args[ARG_stop].u_int) {
        // FIXME: ESP32 also supports 1.5 stop bits
        case 0:
            break;
        case 1:
            uart_set_stop_bits(self->uart_num, UART_STOP_BITS_1);
            self->stop = 1;
            break;
        case 2:
            uart_set_stop_bits(self->uart_num, UART_STOP_BITS_2);
            self->stop = 2;
            break;
        default:
            mp_raise_ValueError("invalid stop bits");
            break;
    }

    // set timeout
    self->timeout = args[ARG_timeout].u_int;
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
    self->uart_num = uart_num;
    self->bits = 8;
    self->parity = 0;
    self->stop = 1;
    self->rts = -1;
    self->cts = -1;
    self->timeout = 0;
    self->patern_chr_num = 3;
    self->patern_char = '+';
    self->data_cb = NULL;
    self->pattern_cb = NULL;
    self->error_cb = NULL;
    self->task_id = NULL;
    self->data_cb_size = 0;
    self->end_task = 0;


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
    if (bufsize < 256) bufsize = 256;
    if (bufsize > 4096) bufsize = 4096;
    self->buffer_size = bufsize;

    // Remove any existing configuration
    uart_driver_delete(self->uart_num);

    // init the peripheral
    // Setup
    uart_param_config(self->uart_num, &uartcfg);

    // RX buffer size is set from argument (default 256), TX buffer is disabled.
    esp_err_t res = uart_driver_install(uart_num, bufsize, 0, 10, &UART_QUEUE[self->uart_num], 0);
    //esp_err_t res = uart_driver_install(uart_num, bufsize, 0, 0, NULL, 0);
    if (res != ESP_OK) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "UART(%d) Error installing driver", uart_num));
    }


    machine_uart_init_helper(self, n_args - 1, args + 1, &kw_args);

    // Make sure pins are connected.
    uart_set_pin(self->uart_num, self->tx, self->rx, self->rts, self->cts);

    //Set uart pattern detect function (This is the test pattern ABCD+++).
    uart_disable_pattern_det_intr(self->uart_num);

    //Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 1024, (void *)self, 12, &self->task_id);

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
    size_t rxbufsize;
    uart_get_buffered_data_len(self->uart_num, &rxbufsize);
    return MP_OBJ_NEW_SMALL_INT(rxbufsize);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_any_obj, machine_uart_any);

//-----------------------------------------------------
STATIC mp_obj_t machine_uart_flush(mp_obj_t self_in) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uart_flush_input(self->uart_num);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_flush_obj, machine_uart_flush);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_uart_callback(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_type, ARG_func, ARG_pattern, ARG_patternlen, ARG_datalen, ARG_timeout, ARG_predelay, ARG_postdelay };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_type,			MP_ARG_REQUIRED | MP_ARG_INT, { .u_int = 0 } },
        { MP_QSTR_func,			MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_pattern_char,	MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_pattern_len,	MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_data_len,		MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_timeout,		MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_pre_delay,	MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
        { MP_QSTR_post_delay,	MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
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
        	    uart_disable_pattern_det_intr(self->uart_num);
            	self->pattern_cb = NULL;
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


    int patlen = -1;
    int datalen = -1;
    int timeout = 10000;
    int predelay = 10;
    int postdelay = 10;
    char pchar = self->patern_char;

    if (MP_OBJ_IS_STR(args[ARG_pattern].u_obj)) {
    	const char * pattern = mp_obj_str_get_str(args[ARG_pattern].u_obj);
    	if ((pattern) && strlen(pattern) > 0) pchar = pattern[0];
	}

	if ((args[ARG_patternlen].u_int > 1) && (args[ARG_patternlen].u_int <= 64)) patlen = args[ARG_patternlen].u_int;
    if ((args[ARG_datalen].u_int >= 0) && (args[ARG_datalen].u_int < self->buffer_size)) datalen = args[ARG_datalen].u_int;

    int tmp = args[ARG_timeout].u_int;
    if ((tmp >= 1) && (tmp <= 200000)) timeout = tmp * 80;
    tmp = args[ARG_predelay].u_int;
    if ((tmp >= 1) && (tmp <= 200000)) predelay = tmp * 80;
    tmp = args[ARG_postdelay].u_int;
    if ((tmp >= 1) && (tmp <= 200000)) postdelay = tmp * 80;

	if (uart_mutex) xSemaphoreTake(uart_mutex, 200 / portTICK_PERIOD_MS);
    switch(cbtype) {
        case UART_CB_TYPE_DATA:
    		if (datalen >= 0) self->data_cb_size = datalen;
    		self->data_cb = args[ARG_func].u_obj;
            break;
        case UART_CB_TYPE_PATTERN:
    	    uart_disable_pattern_det_intr(self->uart_num);
    		self->pattern_cb = args[ARG_func].u_obj;
    		if (patlen > 0) self->patern_chr_num = patlen;
    		if (pchar != self->patern_char) self->patern_char = pchar;
    	    uart_enable_pattern_det_intr(self->uart_num, self->patern_char, self->patern_chr_num, timeout, postdelay, predelay);
    	    //Reset the pattern queue length to record at most 10 pattern positions.
    	    uart_pattern_queue_reset(self->uart_num, 10);
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
	mp_hal_set_wdt_tmo();
    if (self->timeout == 0) {
        bytes_read = uart_read_bytes(self->uart_num, buf_in, size, 0);
    }
    else {
		int wait = self->timeout;
		MP_THREAD_GIL_EXIT();
		while (wait > 0) {
			bytes_read = uart_read_bytes(self->uart_num, buf_in, size, pdMS_TO_TICKS(10));
			if (bytes_read != 0) break;
			mp_hal_reset_wdt();
			wait -= 10;
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

    int bytes_written = uart_write_bytes(self->uart_num, buf_in, size);

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
        uart_get_buffered_data_len(self->uart_num, &rxbufsize);
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
