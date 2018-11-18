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
 * This file is based on 'ftp' from Pycom Limited.
 *
 * Author: LoBo, loboris@gmail.com
 * Copyright (c) 2017, LoBo
 */

#ifndef FTP_H_
#define FTP_H_

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef CONFIG_MICROPY_USE_FTPSERVER

typedef enum {
    E_FTP_STE_DISABLED = 0,
    E_FTP_STE_START,
    E_FTP_STE_READY,
    E_FTP_STE_END_TRANSFER,
    E_FTP_STE_CONTINUE_LISTING,
    E_FTP_STE_CONTINUE_FILE_TX,
    E_FTP_STE_CONTINUE_FILE_RX,
    E_FTP_STE_CONNECTED
} ftp_state_t;

typedef enum {
    E_FTP_STE_SUB_DISCONNECTED = 0,
    E_FTP_STE_SUB_LISTEN_FOR_DATA,
    E_FTP_STE_SUB_DATA_CONNECTED
} ftp_substate_t;

#define FTP_USER_PASS_LEN_MAX	32
#define FTP_DEF_USER            "micro"
#define FTP_DEF_PASS            "python"
#define FTP_MUTEX_TIMEOUT_MS    1000
#define FTP_CMD_TIMEOUT_MS      (CONFIG_MICROPY_FTPSERVER_TIMEOUT*1000)

extern const char *FTP_TAG;
extern char ftp_user[FTP_USER_PASS_LEN_MAX + 1];
extern char ftp_pass[FTP_USER_PASS_LEN_MAX + 1];
extern uint32_t ftp_stack_size;
extern QueueHandle_t ftp_mutex;
extern int ftp_buff_size;
extern int ftp_timeout;

bool ftp_init (void);
void ftp_deinit (void);
int ftp_run (uint32_t elapsed);
bool ftp_enable (void);
bool ftp_isenabled (void);
bool ftp_disable (void);
bool ftp_reset (void);
int ftp_getstate();
bool ftp_terminate (void);
bool ftp_stop_requested();
int32_t ftp_get_maxstack (void);

#endif

#endif /* FTP_H_ */
