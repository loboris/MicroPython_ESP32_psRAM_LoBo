/*
 * This file is based on 'modthreadport' from Pycom Limited.
 *
 * Author: LoBo, https://loboris@github.com, loboris@gmail.com
 * Copyright (c) 2017, LoBo
 */

/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George on behalf of Pycom Ltd
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
#include <sys/time.h>
#include <time.h>

#include "py/mpconfig.h"

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "py/mpstate.h"
#include "py/gc.h"
#include "py/mpthread.h"
#include "py/mphal.h"
#include "mpthreadport.h"

#if defined(CONFIG_MICROPY_USE_TELNET) || defined(CONFIG_MICROPY_USE_FTPSERVER)
#include "tcpip_adapter.h"
#include "esp_wifi_types.h"
#include "esp_wifi.h"
#endif

#ifdef CONFIG_MICROPY_USE_TELNET
#include "libs/telnet.h"

extern TaskHandle_t TelnetTaskHandle;
#endif

#ifdef CONFIG_MICROPY_USE_FTPSERVER
#include "libs/ftp.h"

extern TaskHandle_t FtpTaskHandle;
#endif

extern int MainTaskCore;

TaskHandle_t MainTaskHandle = NULL;

uint8_t main_accept_msg = 1;

// this structure forms a linked list, one node per active thread
//========================
typedef struct _thread_t {
    TaskHandle_t id;					// system id of thread
    int ready;							// whether the thread is ready and running
    void *arg;							// thread Python args, a GC root pointer
    void *stack;						// pointer to the stack
    StaticTask_t *tcb;     				// pointer to the Task Control Block
    size_t stack_len;      				// number of words in the stack
    char name[THREAD_NAME_MAX_SIZE];	// thread name
    QueueHandle_t threadQueue;			// queue used for inter thread communication
    int8_t allow_suspend;
    int8_t suspended;
    int8_t waiting;
    int8_t deleted;
    int16_t notifyed;
    uint16_t type;
    struct _thread_t *next;
} thread_t;

// the mutex controls access to the linked list
STATIC mp_thread_mutex_t thread_mutex;
STATIC thread_t thread_entry0;
STATIC thread_t *thread; // root pointer, handled by mp_thread_gc_others

//-------------------------------
void vPortCleanUpTCB(void *tcb) {
    thread_t *prev = NULL;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; prev = th, th = th->next) {
        // unlink the node from the list
        if (th->tcb == tcb) {
            if (prev != NULL) {
                prev->next = th->next;
            } else {
                // move the start pointer
                thread = th->next;
            }
            // explicitly release all its memory
            if (th->tcb) free(th->tcb);
            if (th->stack) free(th->stack);
            //m_del(thread_t, th, 1);
            free(th);
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
}


// === Initialize the main MicroPython thread ===
//-------------------------------------------------------
void mp_thread_preinit(void *stack, uint32_t stack_len) {
    mp_thread_set_state(&mp_state_ctx.thread);
    // create first entry in linked list of all threads
    thread = &thread_entry0;
    thread->id = xTaskGetCurrentTaskHandle();
    thread->ready = 1;
    thread->arg = NULL;
    thread->stack = stack;
    thread->stack_len = stack_len;
    sprintf(thread->name, "MainThread");
    thread->threadQueue = xQueueCreate( THREAD_QUEUE_MAX_ITEMS, sizeof(thread_msg_t) );
    thread->allow_suspend = 0;
    thread->suspended = 0;
    thread->waiting = 0;
    thread->deleted = 0;
    thread->notifyed = 0;
    thread->type = THREAD_TYPE_MAIN;
    thread->next = NULL;
    MainTaskHandle = thread->id;
}

//-------------------------
void mp_thread_init(void) {
    mp_thread_mutex_init(&thread_mutex);
}

//------------------------------
void mp_thread_gc_others(void) {
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
    	if (th->type == THREAD_TYPE_SERVICE) {
    		continue;
    	}
        gc_collect_root((void**)&th, 1);
        gc_collect_root(&th->arg, 1); // probably not needed
        if (th->id == xTaskGetCurrentTaskHandle()) {
            continue;
        }
        if (!th->ready) {
            continue;
        }
        //ToDo: Check if needed
        gc_collect_root(th->stack, th->stack_len); // probably not needed
    }
    mp_thread_mutex_unlock(&thread_mutex);
}

//--------------------------------------------
mp_state_thread_t *mp_thread_get_state(void) {
    return pvTaskGetThreadLocalStoragePointer(NULL, 1);
}

//-------------------------------------
void mp_thread_set_state(void *state) {
    vTaskSetThreadLocalStoragePointer(NULL, 1, state);
}

//--------------------------
void mp_thread_start(void) {
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
            th->ready = 1;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
}

STATIC void *(*ext_thread_entry)(void*) = NULL;

//-------------------------------------
STATIC void freertos_entry(void *arg) {
    if (ext_thread_entry) {
        ext_thread_entry(arg);
    }
    vTaskDelete(NULL);
}

//------------------------------------------------------------------------------------------------------------------------------
TaskHandle_t mp_thread_create_ex(void *(*entry)(void*), void *arg, size_t *stack_size, int priority, char *name, bool same_core)
{
    // store thread entry function into a global variable so we can access it
    ext_thread_entry = entry;

    // Check thread stack size
    if (*stack_size == 0) {
    	*stack_size = MP_THREAD_DEFAULT_STACK_SIZE; //use default stack size
    }
    else {
        if (*stack_size < MP_THREAD_MIN_STACK_SIZE) *stack_size = MP_THREAD_MIN_STACK_SIZE;
        else if (*stack_size > MP_THREAD_MAX_STACK_SIZE) *stack_size = MP_THREAD_MAX_STACK_SIZE;
    }

    // allocate TCB, stack and linked-list node (must be outside thread_mutex lock)
    //StaticTask_t *tcb = m_new(StaticTask_t, 1);
    //StackType_t *stack = m_new(StackType_t, *stack_size / sizeof(StackType_t));

    // ======================================================================
    // We are NOT going to allocate thread tcb & stack on Micropython heap
    // In case we are using SPI RAM, it can produce some problems and crashes
    // ======================================================================
    StaticTask_t *tcb = NULL;
    StackType_t *stack = NULL;

    tcb = malloc(sizeof(StaticTask_t));
    stack = malloc(*stack_size+256);

    //thread_t *th = m_new_obj(thread_t);
    thread_t *th = (thread_t *)malloc(sizeof(thread_t));

    mp_thread_mutex_lock(&thread_mutex, 1);

    // === Create the thread task ===
	#if CONFIG_MICROPY_USE_BOTH_CORES
    TaskHandle_t id = xTaskCreateStatic(freertos_entry, name, *stack_size, arg, priority, stack, tcb);
	#else
    int run_on_core = MainTaskCore;
	#if !CONFIG_FREERTOS_UNICORE
    if (!same_core) run_on_core = MainTaskCore ^ 1;
	#endif

    TaskHandle_t id = xTaskCreateStaticPinnedToCore(freertos_entry, name, *stack_size, arg, priority, stack, tcb, run_on_core);
	#endif
    if (id == NULL) {
        mp_thread_mutex_unlock(&thread_mutex);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "can't create thread"));
    }

    // adjust the stack_size to provide room to recover from hitting the limit
    //*stack_size -= 1024;

    // add thread to linked list of all threads
    th->id = id;
    th->ready = 0;
    th->arg = arg;
    th->stack = stack;
    th->tcb = tcb;
    th->stack_len = *stack_size;
    th->next = thread;
    snprintf(th->name, THREAD_NAME_MAX_SIZE, name);
    th->threadQueue = xQueueCreate( THREAD_QUEUE_MAX_ITEMS, sizeof(thread_msg_t) );
    th->allow_suspend = 0;
    th->suspended = 0;
    th->waiting = 0;
    th->deleted = 0;
    th->notifyed = 0;
    th->type = THREAD_TYPE_PYTHON;
    thread = th;

    mp_thread_mutex_unlock(&thread_mutex);
    return id;
}

//--------------------------------------------------------------------------------------------------------
void *mp_thread_create(void *(*entry)(void*), void *arg, size_t *stack_size, char *name, bool same_core) {
    return mp_thread_create_ex(entry, arg, stack_size, MP_THREAD_PRIORITY, name, same_core);
}

//---------------------------------------
STATIC void mp_clean_thread(thread_t *th)
{
	if (th->threadQueue) {
		int n = 1;
		while (n) {
			n = uxQueueMessagesWaiting(th->threadQueue);
			if (n) {
				thread_msg_t msg;
				xQueueReceive(th->threadQueue, &msg, 0);
				if (msg.strdata != NULL) free(msg.strdata);
			}
		}
		if (th->threadQueue) vQueueDelete(th->threadQueue);
		th->threadQueue = NULL;
	}
    th->ready = 0;
	th->deleted = 1;
}

//---------------------------
void mp_thread_finish(void) {
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
        	mp_clean_thread(th);
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
}

//---------------------------------------------------
void mp_thread_mutex_init(mp_thread_mutex_t *mutex) {
    mutex->handle = xSemaphoreCreateMutexStatic(&mutex->buffer);
}

//------------------------------------------------------------
int mp_thread_mutex_lock(mp_thread_mutex_t *mutex, int wait) {
    return (pdTRUE == xSemaphoreTake(mutex->handle, wait ? portMAX_DELAY : 0));
}

//-----------------------------------------------------
void mp_thread_mutex_unlock(mp_thread_mutex_t *mutex) {
    xSemaphoreGive(mutex->handle);
}

// Terminate all Python threads
// used before entering sleep/reset
//---------------------------
void mp_thread_deinit(void) {
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        // don't delete the current task
        if (th->id == xTaskGetCurrentTaskHandle()) {
            continue;
        }
    	mp_clean_thread(th);
        vTaskDelete(th->id);
    }
    mp_thread_mutex_unlock(&thread_mutex);
    // allow FreeRTOS to clean-up the threads
    vTaskDelay(2);
}

//--------------------------------------
void mp_thread_allowsuspend(int allow) {
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        // don't allow suspending main task task
        if ((th->id != MainTaskHandle) && (th->id == xTaskGetCurrentTaskHandle())) {
        	th->allow_suspend = allow & 1;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
}

//--------------------------------------
int mp_thread_suspend(TaskHandle_t id) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        // don't suspend the current task
        if (th->id == xTaskGetCurrentTaskHandle()) {
            continue;
        }
        if (th->id == id) {
        	if ((th->allow_suspend) && (th->suspended == 0) && (th->waiting == 0)) {
        		th->suspended = 1;
        		vTaskSuspend(th->id);
        		res = 1;
        	}
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//-------------------------------------
int mp_thread_resume(TaskHandle_t id) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        // don't resume the current task
        if (th->id == xTaskGetCurrentTaskHandle()) {
            continue;
        }
        if (th->id == id) {
        	if ((th->allow_suspend) && (th->suspended) && (th->waiting == 0)) {
        		th->suspended = 0;
        		vTaskResume(th->id);
        		res = 1;
        	}
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//--------------------------
int mp_thread_setblocked() {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
			th->waiting = 1;
			res = 1;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//-----------------------------
int mp_thread_setnotblocked() {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
        	th->waiting = 0;
        	res = 1;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

// Send notification to thread 'id'
// or to all threads if id=0
//-----------------------------------------------------
int mp_thread_notify(TaskHandle_t id, uint32_t value) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if ( (th->id != xTaskGetCurrentTaskHandle()) && ( (id == 0) || (th->id == id) ) ) {
        	res = xTaskNotify(th->id, value, eSetValueWithOverwrite); //eSetValueWithoutOverwrite
        	th->notifyed = 1;
            if (id != 0) break;
        }
    }
    if (id == 0) res = 1;
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//---------------------------------------------
uint32_t mp_thread_getnotify(bool check_only) {
	uint32_t value = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
        	xTaskNotifyWait(0, 0, &value, 0);
      		if (!check_only) {
      			xTaskNotifyWait(ULONG_MAX, ULONG_MAX, NULL, 0);
          		th->notifyed = 0;
      		}
			break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return value;
}

//--------------------------------------------
int mp_thread_notifyPending(TaskHandle_t id) {
	int res = -1;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == id) {
        	res = th->notifyed;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//-----------------------------
void mp_thread_resetPending() {
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
        	th->notifyed = 0;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
}

//------------------------------
uint32_t mp_thread_getSelfID() {
	uint32_t id = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
        	id = (uint32_t)th->id;
			break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return id;
}

//-------------------------------------
int mp_thread_getSelfname(char *name) {
	int res = 0;
	name[0] = '?';
	name[1] = '\0';
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
        	sprintf(name, th->name);
        	res = 1;
			break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//--------------------------------------------------
int mp_thread_getname(TaskHandle_t id, char *name) {
	int res = 0;
	name[0] = '?';
	name[1] = '\0';
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == id) {
        	sprintf(name, th->name);
        	res = 1;
			break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//-------------------------------------------------------------------------------------------------
int mp_thread_semdmsg(TaskHandle_t id, int type, uint32_t msg_int, uint8_t *buf, uint32_t buflen) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        // don't send to the current task or service thread
        if ((th->id == xTaskGetCurrentTaskHandle()) || (th->type == THREAD_TYPE_SERVICE)) {
            continue;
        }
        if ((id == 0) || (th->id == id)) {
        	if (th->threadQueue == NULL) break;
    		thread_msg_t msg;
    	    struct timeval tv;
    	    uint64_t tmstamp;
    	    gettimeofday(&tv, NULL);
    	    tmstamp = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    	    msg.timestamp = tmstamp;
			msg.sender_id = xTaskGetCurrentTaskHandle();
			if (type == THREAD_MSG_TYPE_INTEGER) {
				msg.intdata = msg_int;
				msg.strdata = NULL;
				msg.type = type;
				res = 1;
			}
			else if (type == THREAD_MSG_TYPE_STRING) {
				msg.intdata = buflen;
				msg.strdata = malloc(buflen+1);
				if (msg.strdata != NULL) {
					memcpy(msg.strdata, buf, buflen);
					msg.strdata[buflen] = 0;
					msg.type = type;
					res = 1;
				}
			}
			if (res) {
				if (xQueueSend(th->threadQueue, &msg, 0) != pdTRUE) res = 0;
			}
            if (id != 0) break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//------------------------------------------------------------------------------------------
int mp_thread_getmsg(uint32_t *msg_int, uint8_t **buf, uint32_t *buflen, uint32_t *sender) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        // get message for current task
        if ((th->id == xTaskGetCurrentTaskHandle()) && (th->type != THREAD_TYPE_SERVICE)) {
        	if (th->threadQueue == NULL) break;

        	thread_msg_t msg;
        	if (xQueueReceive(th->threadQueue, &msg, 0) == pdTRUE) {
        		*sender = (uint32_t)msg.sender_id;
        		if (msg.type == THREAD_MSG_TYPE_INTEGER) {
        			*msg_int = msg.intdata;
        			*buflen = 0;
        			res = THREAD_MSG_TYPE_INTEGER;
        		}
        		else if (msg.type == THREAD_MSG_TYPE_STRING) {
        			*msg_int = 0;
        			if ((msg.strdata != NULL) && (msg.intdata > 0)) {
            			*buflen = msg.intdata;
            			*buf = msg.strdata;
            			res = THREAD_MSG_TYPE_STRING;
        			}
        		}
        	}
        	break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);

    return res;
}

//-------------------------------------
int mp_thread_status(TaskHandle_t id) {
	int res = -1;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if ((th->id == xTaskGetCurrentTaskHandle()) || (th->type == THREAD_TYPE_SERVICE)) {
            continue;
        }
        if (th->id == id) {
			if (!th->deleted) {
				if (th->suspended) res = 1;
				else if (th->waiting) res = 2;
				else res = 0;
			}
			break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//---------------------------------------
int mp_thread_list(thread_list_t *list) {
	int num = 0;

    mp_thread_mutex_lock(&thread_mutex, 1);

    for (thread_t *th = thread; th != NULL; th = th->next) {
    	num++;
    }
    if ((num == 0) || (list == NULL)) {
        mp_thread_mutex_unlock(&thread_mutex);
    	return num;
    }

	list->nth = num;
	list->threads = malloc(sizeof(threadlistitem_t) * (num+1));
	if (list->threads == NULL) num = 0;
	else {
		int nth = 0;
		threadlistitem_t *thr = NULL;
		uint32_t min_stack;
		for (thread_t *th = thread; th != NULL; th = th->next) {
			thr = list->threads + (sizeof(threadlistitem_t) * nth);
	        if (th->id == xTaskGetCurrentTaskHandle()) min_stack = uxTaskGetStackHighWaterMark(NULL);
	        else min_stack = uxTaskGetStackHighWaterMark(th->id);

			thr->id = (uint32_t)th->id;
			sprintf(thr->name, "%s", th->name);
			thr->suspended = th->suspended;
			thr->waiting = th->waiting;
			thr->type = th->type;
			thr->stack_len = th->stack_len;
			thr->stack_max = th->stack_len - min_stack;
			nth++;
			if (nth > num) break;
		}
		if (nth != num) {
			free(list->threads);
			list->threads = NULL;
			num = 0;
		}
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return num;
}

//------------------------------------------
int mp_thread_replAcceptMsg(int8_t accept) {
	int res = main_accept_msg;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
    	 if (th->id == xTaskGetCurrentTaskHandle()) {
    		 if ((th->id == MainTaskHandle) && (accept >= 0)) {
    			 main_accept_msg = accept & 1;
    		 }
			 break;
    	 }
    }
    mp_thread_mutex_unlock(&thread_mutex);

    return res;
}


// ===== SERVICE THREADS ==============================================

#if defined(CONFIG_MICROPY_USE_TELNET) || defined(CONFIG_MICROPY_USE_FTPSERVER)
// Check if WiFi connection is available
//----------------------
static int _check_wifi()
{
    tcpip_adapter_if_t if_type;
    tcpip_adapter_ip_info_t info;
    wifi_mode_t wifi_mode;

    esp_wifi_get_mode(&wifi_mode);
    if (wifi_mode == WIFI_MODE_AP) if_type = TCPIP_ADAPTER_IF_AP;
    else if (wifi_mode == WIFI_MODE_STA) if_type = TCPIP_ADAPTER_IF_STA;
    else return 2;
    tcpip_adapter_get_ip_info(if_type, &info);
    if (info.ip.addr == 0) return 0;
    return 1;
}
#endif

#ifdef CONFIG_MICROPY_USE_TELNET
//===================================
void telnet_task (void *pvParameters)
{
    int res;
    // Initialize telnet, create rx buffer and mutex
    telnet_init();

    // Check if WiFi connection is available
    res = _check_wifi();
    while ( res == 0) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        res = _check_wifi();
    }
    if (res == 2) goto exit;

    // We have WiFi connection, enable telnet
    telnet_enable();

    while (1) {
        res = telnet_run();
        if ( res < 0) {
            if (res == -1) {
                ESP_LOGD("[Telnet]", "\nRun Error");
            }
            break;
        }

        vTaskDelay(1);

        // ---- Check if WiFi is still available ----
        res = _check_wifi();
        if (res == 0) {
            bool was_enabled = telnet_isenabled();
            telnet_disable();
            while ( res == 0) {
                vTaskDelay(200 / portTICK_PERIOD_MS);
                res = _check_wifi();
                if (res == 2) goto exit;
            }
            if (was_enabled) telnet_enable();
        }
        else if (res == 2) break;
        // ------------------------------------------
    }
exit:
    telnet_disable();
    telnet_deinit();
    ESP_LOGD("[Telnet]", "\nTask terminated!");
    TelnetTaskHandle = NULL;
    vSemaphoreDelete(telnet_mutex);
    telnet_mutex = NULL;
    vTaskDelete(NULL);
}

//-----------------------------------------------------
uintptr_t mp_thread_createTelnetTask(size_t stack_size)
{
    if (TelnetTaskHandle == NULL) {
        telnet_stack_size = stack_size;
        #if CONFIG_MICROPY_USE_BOTH_CORES
        xTaskCreate(&telnet_task, "Telnet", stack_size, NULL, CONFIG_MICROPY_TASK_PRIORITY, &TelnetTaskHandle);
        #else
        xTaskCreatePinnedToCore(&telnet_task, "Telnet", stack_size, NULL, CONFIG_MICROPY_TASK_PRIORITY, &TelnetTaskHandle, MainTaskCore);
        #endif
    }
    return (uintptr_t)TelnetTaskHandle;
}
#endif


#ifdef CONFIG_MICROPY_USE_FTPSERVER

//================================
void ftp_task (void *pvParameters)
{
    int res;
    uint64_t elapsed, time_ms = mp_hal_ticks_ms();
    // Initialize ftp, create rx buffer and mutex
    ftp_init();

    res = _check_wifi();
    while ( res == 0) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        res = _check_wifi();
    }
    if (res == 2) goto exit;

    // We have WiFi connection, enable ftp
    ftp_enable();

    time_ms = mp_hal_ticks_ms();
    while (1) {
        elapsed = mp_hal_ticks_ms() - time_ms;
        time_ms = mp_hal_ticks_ms();

        res = ftp_run(elapsed);
        if (res < 0) {
            if (res == -1) {
                ESP_LOGD("[Ftp]", "\nRun Error");
            }
            break;
        }

        vTaskDelay(1);

        // ---- Check if WiFi is still available ----
        res = _check_wifi();
        if (res == 0) {
            bool was_enabled = ftp_isenabled();
            ftp_disable();
            while ( res == 0) {
                vTaskDelay(200 / portTICK_PERIOD_MS);
                res = _check_wifi();
                if (res == 2) goto exit;
            }
            if (was_enabled) ftp_enable();
        }
        else if (res == 2) break;
        // ------------------------------------------
    }
exit:
    ftp_disable();
    ftp_deinit();
    ESP_LOGD("[Ftp]", "\nTask terminated!");
    FtpTaskHandle = NULL;
    vSemaphoreDelete(ftp_mutex);
    ftp_mutex = NULL;
    vTaskDelete(NULL);
}

//--------------------------------------------------
uintptr_t mp_thread_createFtpTask(size_t stack_size)
{
    if (FtpTaskHandle == NULL) {
        ftp_stack_size = stack_size;
        #if CONFIG_MICROPY_USE_BOTH_CORES
        xTaskCreate(&ftp_task, "FtpServer", stack_size, NULL, CONFIG_MICROPY_TASK_PRIORITY, &FtpTaskHandle);
        #else
        xTaskCreatePinnedToCore(&ftp_task, "FtpServer", stack_size, NULL, CONFIG_MICROPY_TASK_PRIORITY, &FtpTaskHandle, MainTaskCore);
        #endif
    }
    return (uintptr_t)FtpTaskHandle;
}
#endif
