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
/*
 * This file is based on 'telnet' from Pycom Limited.
 *
 * Author: LoBo, loboris@gmail.com
 * Copyright (c) 2017, LoBo
 */

#ifndef TELNET_H_
#define TELNET_H_

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_TELNET

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TELNET_LOGIN_MSG_LEN_MAX    128 // set this to 0 to use static login msg
#define TELNET_USER_PASS_LEN_MAX	32
#define TELNET_DEF_USER             "micro"
#define TELNET_DEF_PASS             "python"
#define TELNET_DEF_TIMEOUT_MS       300000	// 5 minutes
#define TELNET_MUTEX_TIMEOUT_MS       1000

typedef enum {
    E_TELNET_STE_DISABLED = 0,
    E_TELNET_STE_START,
    E_TELNET_STE_LISTEN,
    E_TELNET_STE_CONNECTED,
    E_TELNET_STE_LOGGED_IN
} telnet_state_t;


extern char telnet_user[TELNET_USER_PASS_LEN_MAX + 1];
extern char telnet_pass[TELNET_USER_PASS_LEN_MAX + 1];
char *telnet_login_success;
extern uint32_t telnet_stack_size;
extern QueueHandle_t telnet_mutex;
extern int telnet_timeout;

/******************************************************************************
 DECLARE PUBLIC FUNCTIONS
 ******************************************************************************/

void telnet_init (void);
void telnet_deinit (void);
int telnet_run (void);
void telnet_tx_strn (const char *str, int len);
bool telnet_rx_any (void);
bool telnet_loggedin (void);
int  telnet_rx_char (void);
bool telnet_enable (void);
bool telnet_disable (void);
bool telnet_isenabled (void);
bool telnet_reset (void);
int telnet_getstate();
bool telnet_terminate (void);
bool telnet_stop_requested();
int32_t telnet_get_maxstack (void);

#endif

#endif /* TELNET_H_ */
