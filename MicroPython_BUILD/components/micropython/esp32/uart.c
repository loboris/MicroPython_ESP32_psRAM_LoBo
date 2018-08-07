/*
 * This file is part of the MicroPython project, http://micropython.org/
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

#include "driver/uart.h"
#include "uart.h"

#include "py/mpstate.h"
#include "py/mphal.h"

STATIC void uart_irq_handler(void *arg);

QueueHandle_t uart0_mutex = NULL;
QueueSetMemberHandle_t uart0_semaphore = NULL;
int uart0_raw_input = 0;
static uart_isr_handle_t uart0_handle = NULL;

//------------------
void uart_init(void)
{
	if (uart0_mutex == NULL) uart0_mutex = xSemaphoreCreateMutex();
	if (uart0_semaphore == NULL) uart0_semaphore = xSemaphoreCreateBinary();

	if (uart0_handle == NULL) {
		uart_isr_register(UART_NUM_0, uart_irq_handler, NULL, ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM, &uart0_handle);
		uart_enable_rx_intr(UART_NUM_0);
	}
}

// all code executed in ISR must be in IRAM, and any const data must be in DRAM
//-----------------------------------------------
STATIC void IRAM_ATTR uart_irq_handler(void *arg)
{
    volatile uart_dev_t *uart = &UART0;
    static int xHigherPriorityTaskWoken;

    uart->int_clr.rxfifo_full = 1;
    uart->int_clr.frm_err = 1;
    uart->int_clr.rxfifo_tout = 1;
    xHigherPriorityTaskWoken = pdFALSE;
    while (uart->status.rxfifo_cnt) {
        uint8_t c = uart->fifo.rw_byte;
		if ((uart0_raw_input == 0) && (c == mp_interrupt_char)) {
			// inline version of mp_keyboard_interrupt();
			MP_STATE_VM(mp_pending_exception) = MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_kbd_exception));
			#if MICROPY_ENABLE_SCHEDULER
			if (MP_STATE_VM(sched_state) == MP_SCHED_IDLE) {
				MP_STATE_VM(sched_state) = MP_SCHED_PENDING;
			}
			#endif
		}
		else {
			// this is an inline function so will be in IRAM
			ringbuf_put(&stdin_ringbuf, c);
		}
    }
    xSemaphoreGiveFromISR( uart0_semaphore, &xHigherPriorityTaskWoken );}
