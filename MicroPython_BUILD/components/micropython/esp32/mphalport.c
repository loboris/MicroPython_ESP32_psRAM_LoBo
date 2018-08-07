/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Damien P. George
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
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/uart.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "py/obj.h"
#include "py/mpstate.h"
#include "py/mphal.h"
#include "extmod/misc.h"
#include "lib/utils/pyexec.h"
#include "uart.h"
#include "machine_rtc.h"
#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_TELNET
#include "telnet.h"
#endif

uint32_t mp_hal_wdg_rst_tmo = 0;
RTC_DATA_ATTR uint64_t mp_hal_ticks_base;

static bool stdin_disable = false;
static char stdin_enable_pattern[16] = "";

STATIC uint8_t stdin_ringbuf_array[CONFIG_MICROPY_RX_BUFFER_SIZE];
ringbuf_t stdin_ringbuf = {stdin_ringbuf_array, sizeof(stdin_ringbuf_array), 0, 0};

extern void mp_handle_pending(void);

//--------------------------------
void disableStdin(const char *pat)
{
	snprintf(stdin_enable_pattern, 15, "%s", pat);
	stdin_disable = true;
	char wpat[64] = {'\0'};
	char hpat[5] = {'\0'};
	for (int i=0; i<16; i++) {
	    if (stdin_enable_pattern[i] == 0) break;
	    if ((stdin_enable_pattern[i] < 0x20) || (stdin_enable_pattern[i] < 0x20)) sprintf(hpat, "\\x%02X", stdin_enable_pattern[i]);
	    else sprintf(hpat, "%c", stdin_enable_pattern[i]);
	    strcat(wpat, hpat);
	}
	ESP_LOGI("[stdin]", "Disabled, waiting for pattern [%s]", wpat);
}

//---------------------
void mp_hal_reset_wdt()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t now = tv.tv_sec * 1000 + (tv.tv_usec / 1000);
	if (now > mp_hal_wdg_rst_tmo) {
		#ifdef CONFIG_MICROPY_USE_TASK_WDT
	    esp_task_wdt_reset();
		#endif
		#if CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU0 || CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1
		vTaskDelay(1); // allow other core idle task to reset the watchdog
		#endif
	    mp_hal_wdg_rst_tmo = now  + (CONFIG_TASK_WDT_TIMEOUT_S * 500);
	}
}

//-----------------------
void mp_hal_set_wdt_tmo()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    mp_hal_wdg_rst_tmo = (tv.tv_sec * 1000 + (tv.tv_usec / 1000))  + (CONFIG_TASK_WDT_TIMEOUT_S * 500);
	#ifdef CONFIG_MICROPY_USE_TASK_WDT
	esp_task_wdt_reset();
	#endif
}


// wait until at least one character is received or the timeout expires
//---------------------------------------
int mp_hal_stdin_rx_chr(uint32_t timeout)
{
	uint64_t wait_end = mp_hal_ticks_ms() + timeout;
	int c = -1;
	char pattern[16];
	uint8_t pattern_idx = 0;

    for (;;) {
		#ifdef CONFIG_MICROPY_USE_TASK_WDT
		esp_task_wdt_reset();
		#endif
    	if (mp_hal_ticks_ms() > wait_end) return -1;

		#ifdef CONFIG_MICROPY_USE_TELNET
		// read telnet first
		if (telnet_rx_any()) return telnet_rx_char();
		#endif

		c = ringbuf_get(&stdin_ringbuf);
    	if (c < 0) {
    		// no character in ring buffer
        	// wait max 10 ms for character
    	   	MP_THREAD_GIL_EXIT();
        	if ( xSemaphoreTake( uart0_semaphore, 10 / portTICK_PERIOD_MS ) == pdTRUE ) {
        		// received
        	   	MP_THREAD_GIL_ENTER();
                c = ringbuf_get(&stdin_ringbuf);
        	}
        	else {
        		// not received
        	   	MP_THREAD_GIL_ENTER();
        		c = -1;
        	}
    	}
    	if (c >= 0) {
    		// Character received
			#ifdef CONFIG_MICROPY_USE_TELNET
    		if (telnet_loggedin()) {
    			if (c == 20) {
    				// Ctrl_T received, reset telnet
    				telnet_reset();
    	        	printf("[Telnet] Connection terminated from REPL\n");
    			}
	            return -1;
    		}
			#endif

    		if (stdin_disable) {
    			if (strlen(stdin_enable_pattern) > 0) {
					// Check stdin enable pattern
					pattern[pattern_idx++] = c;
					pattern[pattern_idx] = '\0';
					if (strstr(stdin_enable_pattern, pattern) == stdin_enable_pattern) {
						if (strlen(stdin_enable_pattern) == strlen(pattern)) {
							// pattern received, enable stdin
							stdin_disable = false;
						    ESP_LOGI("[stdin]", "Pattern matched, enabled");
						}
					}
					else if (stdin_disable) pattern_idx = 0;
    			}
    			if (stdin_disable) continue;
    			return -1;
    		}
    		return c;
    	}

        xSemaphoreTake(uart0_mutex, UART_SEMAPHORE_WAIT);
        int raw = uart0_raw_input;
    	xSemaphoreGive(uart0_mutex);
        if (raw == 0) {
        	// check pending exception
        	//MICROPY_EVENT_POLL_HOOK
	        mp_handle_pending();
        }
    }
    return -1;
}

#ifdef CONFIG_MICROPY_USE_TELNET
// Convert '\n' to '\r\n'
//-------------------------------------------------------------
static void telnet_stdout_tx_str(const char *str, uint32_t len)
{
	char *tstr = malloc(len*2+1);
	char prev = '\0';
	uint32_t idx = 0;
	while (len--) {
		if ((*str == '\n') && (prev != '\r')) tstr[idx++] = '\r';
		tstr[idx++] = *str;
		prev = *str++;
	}
	tstr[idx++] = '\0';
	telnet_tx_strn(tstr, strlen(tstr));
	free(tstr);
}
#endif

// send newline character to printf channel
//-------------------------------
void mp_hal_stdout_tx_newline() {
	#ifdef CONFIG_MICROPY_USE_TELNET
   	if (telnet_loggedin()) telnet_tx_strn("\r\n", 2);
   	else {
   		uart_tx_one_char('\n');
   	}
	#else
   	uart_tx_one_char('\n');
	#endif
}

// send NULL ending string to printf channel
//------------------------------------------
void mp_hal_stdout_tx_str(const char *str) {
	if (str == NULL) return;
	#ifdef CONFIG_MICROPY_USE_TELNET
   	if (telnet_loggedin()) telnet_tx_strn(str, strlen(str));
   	else {
   	   	MP_THREAD_GIL_EXIT();
   	    while (*str) {
   	        uart_tx_one_char(*str++);
   	    }
   	   	MP_THREAD_GIL_ENTER();
   	}
	#else
   	MP_THREAD_GIL_EXIT();
    while (*str) {
        uart_tx_one_char(*str++);
    }
   	MP_THREAD_GIL_ENTER();
	#endif
}

// send 'len' characters from string buffer to printf channel
//---------------------------------------------------------
void mp_hal_stdout_tx_strn(const char *str, uint32_t len) {
	if (str == NULL) return;
	#ifdef CONFIG_MICROPY_USE_TELNET
   	if (telnet_loggedin()) telnet_tx_strn(str, len);
   	else {
   	   	MP_THREAD_GIL_EXIT();
   	    while (len--) {
   	        uart_tx_one_char(*str++);
   	    }
   		MP_THREAD_GIL_ENTER();
   	}
	#else
   	MP_THREAD_GIL_EXIT();
    while (len--) {
        uart_tx_one_char(*str++);
    }
	MP_THREAD_GIL_ENTER();
	#endif
}

// Efficiently convert "\n" to "\r\n"
//----------------------------------------------------------------
void mp_hal_stdout_tx_strn_cooked(const char *str, uint32_t len) {
	if (str == NULL) return;
	#ifdef CONFIG_MICROPY_USE_TELNET
   	if (telnet_loggedin()) telnet_stdout_tx_str(str, len);
   	else {
   	   	MP_THREAD_GIL_EXIT();
   	    while (len--) {
   	        if (*str == '\n') {
   	            uart_tx_one_char('\r');
   	        }
   	        uart_tx_one_char(*str++);
   	    }
   	   	MP_THREAD_GIL_ENTER();
   	}
	#else
   	MP_THREAD_GIL_EXIT();
    while (len--) {
		if (*str == '\n') {
			uart_tx_one_char('\r');
		}
		uart_tx_one_char(*str++);
    }
   	MP_THREAD_GIL_ENTER();
	#endif
}

//----------------------
uint64_t getTicks_base()
{
	uint64_t ticks_base = 0;
	if (sntp_mutex) {
		if (xSemaphoreTake(sntp_mutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
			ticks_base = mp_hal_ticks_base;
			xSemaphoreGive(sntp_mutex);
		}
	}
	else ticks_base = mp_hal_ticks_base;
	return ticks_base;
}

//-------------------------------------
void setTicks_base(uint64_t ticks_base)
{
	if (sntp_mutex) {
		if (xSemaphoreTake(sntp_mutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
			mp_hal_ticks_base = ticks_base;
			xSemaphoreGive(sntp_mutex);
		}
	}
	else mp_hal_ticks_base = ticks_base;
}

//------------------------------
uint64_t mp_hal_ticks_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((((uint64_t)tv.tv_sec * 1000000) + (uint64_t)tv.tv_usec) - getTicks_base()) / 1000;
}

//------------------------------
uint64_t mp_hal_ticks_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((((uint64_t)tv.tv_sec * 1000000) + (uint64_t)tv.tv_usec) - getTicks_base());
}

/*
 * Delay specified number of milli seconds
 * For the delay time up to 10 ms the function is blocking
 * For delay times greater than 10 ms, the function
 * does not block the execution of the other threads.
 * Waiting can be interrupted if the thread receives the notification
 *
 * Returns the actual delay time.
 *
 */
//------------------------------
int mp_hal_delay_ms(uint32_t ms)
{
	if (ms == 0) return 0;

	#ifdef CONFIG_MICROPY_USE_TASK_WDT
	esp_task_wdt_reset();
	#endif

	if (ms <= 2) {
		// For delays up to 2 ms we use blocking delay
	    ets_delay_us(ms * 1000);
	    return ms;
	}

	uint64_t tticks_base = getTicks_base();
	struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t tstart = ((uint32_t)tv.tv_sec * 1000) + ((uint32_t)tv.tv_usec / 1000);
	uint32_t tend = tstart;
	uint32_t nres = tstart + (CONFIG_TASK_WDT_TIMEOUT_S * 500);

	MP_THREAD_GIL_EXIT();

	int ncheck = 0;
	while (1) {
		if (tticks_base != getTicks_base()) break;
        gettimeofday(&tv, NULL);
        tend = ((uint32_t)tv.tv_sec * 1000) + ((uint32_t)tv.tv_usec / 1000);
        if ((tend-tstart) >= ms) break;

        vTaskDelay(2);

        #ifdef CONFIG_MICROPY_USE_TASK_WDT
        if (tend > nres) {
        	esp_task_wdt_reset();
        	nres = tend + (CONFIG_TASK_WDT_TIMEOUT_S * 500);
        }
		#endif
		// Break if notification received
        ncheck++;
        if (ncheck >= 50) {
        	ncheck = 0;
        	if (mp_thread_getnotify(1)) break;
        }
	}

    MP_THREAD_GIL_ENTER();
    return (tend - tstart);
}

//---------------------------------
void mp_hal_delay_us(uint32_t us) {
	if (us == 0) return;
	if (us > 10000) {
		// Delay greater then 10 ms, use ms delay function
		uint32_t dus = us % 10000;	// remaining micro seconds
		mp_hal_delay_ms(us/10000);
	    if (dus) ets_delay_us(dus);
		return;
	}
    ets_delay_us(us);
}

// this function could do with improvements (eg use ets_delay_us)
//--------------------------------------
void mp_hal_delay_us_fast(uint32_t us) {
    ets_delay_us(us);
}
