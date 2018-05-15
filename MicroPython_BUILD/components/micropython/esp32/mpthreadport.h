/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George on behalf of Pycom Ltd
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

#ifndef __MICROPY_INCLUDED_ESP32_MPTHREADPORT_H__
#define __MICROPY_INCLUDED_ESP32_MPTHREADPORT_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "sdkconfig.h"


// Thread types
#define THREAD_TYPE_MAIN		1
#define THREAD_TYPE_PYTHON		2
#define THREAD_TYPE_SERVICE		3
#define THREAD_TYPE_REPL		3

// Reserved thread notification constants
#define THREAD_NOTIFY_PAUSE		0x01000000
#define THREAD_NOTIFY_RESUME	0x02000000
#define THREAD_NOTIFY_EXIT		0x04000000
#define THREAD_NOTIFY_STATUS	0x08000000
#define THREAD_NOTIFY_RESET		0x10000000
#define THREAD_WAIT_TIMEOUT		0x20000000

#define THREAD_STATUS_RUNNING		0
#define THREAD_STATUS_SUSPENDED		1
#define THREAD_STATUS_WAITING		2
#define THREAD_STATUS_TERMINATED	-1

#ifdef CONFIG_MICROPY_USE_TELNET
#define TELNET_STACK_SIZE	(1580)
#define TELNET_STACK_LEN	(TELNET_STACK_SIZE / sizeof(StackType_t))
#endif

#ifdef CONFIG_MICROPY_USE_FTPSERVER
#define FTP_STACK_SIZE	(6*1024)
#define FTP_STACK_LEN	(FTP_STACK_SIZE / sizeof(StackType_t))
#endif

//ToDo: Check if thread can run on different priority than main task
//#if CONFIG_MICROPY_THREAD_PRIORITY > CONFIG_MICROPY_TASK_PRIORITY
//#define MP_THREAD_PRIORITY	CONFIG_MICROPY_THREAD_PRIORITY
//#else
#define MP_THREAD_PRIORITY	CONFIG_MICROPY_TASK_PRIORITY
//#endif

#define MP_THREAD_MIN_STACK_SIZE			1580
#define MP_THREAD_MIN_SERVICE_STACK_SIZE	(2*1024)
#define MP_THREAD_DEFAULT_STACK_SIZE		(CONFIG_MICROPY_THREAD_STACK_SIZE*1024)
#define MP_THREAD_MAX_STACK_SIZE			(48*1024)

typedef struct _mp_thread_mutex_t {
    SemaphoreHandle_t handle;
    StaticSemaphore_t buffer;
} mp_thread_mutex_t;

#define THREAD_NAME_MAX_SIZE		16
#define THREAD_MGG_BROADCAST		0xFFFFEEEE
#define THREAD_MSG_TYPE_NONE		0
#define THREAD_MSG_TYPE_INTEGER		1
#define THREAD_MSG_TYPE_STRING		2
#define MAX_THREAD_MESSAGES			8
#define THREAD_QUEUE_MAX_ITEMS		8

// this structure is used for inter-thread communication/data passing
typedef struct _thread_msg_t {
    int type;						// message type
    TaskHandle_t sender_id;			// id of the message sender
    uint32_t intdata;					// integer data or string data length
    uint8_t *strdata;				// string data
    uint32_t timestamp;				// message timestamp in ms
} thread_msg_t;

typedef struct _thread_listitem_t {
    uint32_t id;						// thread id
    char name[THREAD_NAME_MAX_SIZE];	// thread name
    uint8_t suspended;
    uint8_t waiting;
    uint8_t type;
    uint32_t stack_len;
    uint32_t stack_max;
} threadlistitem_t;

typedef struct _thread_list_t {
    int nth;						// number of active threads
    threadlistitem_t *threads;		// pointer to thread info
} thread_list_t;

extern TaskHandle_t MainTaskHandle;
extern TaskHandle_t ReplTaskHandle;

thread_msg_t thread_messages[MAX_THREAD_MESSAGES];

uint8_t main_accept_msg;

void mp_thread_preinit(void *stack, uint32_t stack_len);
int mp_thread_num_threads();
void mp_thread_gc_others(int flag);

void mp_thread_allowsuspend(int allow);
int mp_thread_suspend(TaskHandle_t id);
int mp_thread_resume(TaskHandle_t id);
int mp_thread_notify(TaskHandle_t id, uint32_t value);
uint32_t mp_thread_getnotify(bool check_only);
int mp_thread_notifyPending(TaskHandle_t id);
void mp_thread_resetPending();
int mp_thread_semdmsg(TaskHandle_t id, int type, uint32_t msg_int, uint8_t *buf, uint32_t buflen);
int mp_thread_getmsg(uint32_t *msg_int, uint8_t **buf, uint32_t *buflen, uint32_t *sender);
int mp_thread_status(TaskHandle_t id);

int mp_thread_set_sp(void *sp, void *top);
int mp_thread_get_sp(void);
int mp_thread_set_ptrs(void **ptrs, size_t size);
int mp_thread_setblocked();
int mp_thread_setnotblocked();

uint32_t mp_thread_getSelfID();
int mp_thread_getSelfname(char *name);
int mp_thread_getname(TaskHandle_t id, char *name);
int mp_thread_list(thread_list_t *list);
int mp_thread_replAcceptMsg(int8_t accept);
int mp_thread_mainAcceptMsg(int8_t accept);

#ifdef CONFIG_MICROPY_USE_TELNET
uintptr_t mp_thread_createTelnetTask(size_t stack_size);
#endif
#ifdef CONFIG_MICROPY_USE_FTPSERVER
uintptr_t mp_thread_createFtpTask(size_t stack_size);
#endif


#endif // __MICROPY_INCLUDED_ESP32_MPTHREADPORT_H__
