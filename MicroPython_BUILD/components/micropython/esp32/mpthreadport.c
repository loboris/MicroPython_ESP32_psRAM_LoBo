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

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "py/mpconfig.h"

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "soc/cpu.h"

#include "py/mpstate.h"
#include "py/gc.h"
#include "py/mpthread.h"
#include "py/mphal.h"
#include "mpthreadport.h"
#include "modmachine.h"

#if defined(CONFIG_MICROPY_USE_TELNET) || defined(CONFIG_MICROPY_USE_FTPSERVER)
#include "libs/libGSM.h"
#include "modnetwork.h"
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
TaskHandle_t ReplTaskHandle = NULL;

uint8_t main_accept_msg = 1;

// this structure forms a linked list, one node per active thread
//========================
typedef struct _thread_t {
    TaskHandle_t id;					// system id of thread
    int ready;							// whether the thread is ready and running
    void *arg;							// thread Python args, a GC root pointer
    void *stack;						// pointer to the stack
    void *curr_sp;						// current stack pointer
    void *stack_top;					// stack top
    StaticTask_t *tcb;     				// pointer to the Task Control Block
    size_t stack_len;      				// number of words in the stack
    void **ptrs;						// root pointers
    size_t ptrs_len;					// length of root pointers
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
STATIC thread_t *thread = NULL; // root pointer, handled by mp_thread_gc_others

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void vPortCleanUpTCB(void *tcb)
{
	if ((MainTaskHandle) && (thread)) {
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
}


// === Initialize the main MicroPython thread ===
//-----------------------------------------------------
void mp_thread_preinit(void *stack, uint32_t stack_len)
{
    // Initialize threads mutex
    mp_thread_mutex_init(&thread_mutex);

    mp_thread_set_state(&mp_state_ctx.thread);
    // create first entry in linked list of all threads
    thread = &thread_entry0;
    thread->id = xTaskGetCurrentTaskHandle();
    thread->ready = 1;
    thread->arg = NULL;
    thread->stack = stack;
    thread->curr_sp = stack+stack_len;
    thread->stack_top = stack+stack_len;
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

//----------------------------------
void mp_thread_gc_others(int flag) {
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (!th->ready) {
            continue;
        }
    	if (th->type == THREAD_TYPE_SERVICE) continue;

		if (th->arg) {
			#if MICROPY_PY_GC_COLLECT_RETVAL
			n_marked = MP_STATE_MEM(gc_marked);
			#endif
			// Mark the pointers on thread arguments
			gc_collect_root(&th->arg, 1);
			#if MICROPY_PY_GC_COLLECT_RETVAL
			if (flag) printf("th_collect:    marked on arg: %d (%s)\n", MP_STATE_MEM(gc_marked) - n_marked, th->name);
			#endif
		}

		if (th->id == xTaskGetCurrentTaskHandle()) continue;

		#if MICROPY_PY_GC_COLLECT_RETVAL
		n_marked = MP_STATE_MEM(gc_marked);
		#endif
		// Mark the thread root pointers
	    gc_collect_root(th->ptrs, th->ptrs_len);
        //gc_collect_root((void**)&th, 1);
		#if MICROPY_PY_GC_COLLECT_RETVAL
		if (flag) printf("th_collect:   marked on thrd: %d (%s)\n", MP_STATE_MEM(gc_marked) - n_marked, th->name);
		#endif

		#if MICROPY_PY_GC_COLLECT_RETVAL
		n_marked = MP_STATE_MEM(gc_marked);
		#endif
		// Mark the pointers on thread stack
		gc_collect_root(th->curr_sp, (th->stack_top - th->curr_sp) / sizeof(uint32_t));

		#if MICROPY_PY_GC_COLLECT_RETVAL
		if (flag) printf("th_collect:  marked on stack: %d (%s) [%p - %p]\n",
				MP_STATE_MEM(gc_marked) - n_marked, th->name, th->curr_sp, th->stack_top);
		#endif
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

//---------------------------------------------------------------------------------------------------------------------------------
TaskHandle_t mp_thread_create_ex(void *(*entry)(void*), void *arg, size_t *in_stack_size, int priority, char *name, bool same_core)
{
	size_t stack_size = *in_stack_size;
	bool is_repl = (strcmp(name, "REPLthread") == 0);
	if (is_repl) {
		if (ReplTaskHandle) {
	        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "REPL thread already started"));
		}
	}
	// store thread entry function into a global variable so we can access it
    ext_thread_entry = entry;

    // Check thread stack size
    if (stack_size == 0) {
    	stack_size = MP_THREAD_DEFAULT_STACK_SIZE; //use default stack size
    }
    else {
        if (stack_size < MP_THREAD_MIN_STACK_SIZE) stack_size = MP_THREAD_MIN_STACK_SIZE;
        else if (stack_size > MP_THREAD_MAX_STACK_SIZE) stack_size = MP_THREAD_MAX_STACK_SIZE;
    }
    stack_size &= 0x7FFFFFF8;

    // === Allocate TCB, stack and linked-list node ===
    StaticTask_t *tcb = NULL;
    StackType_t *stack = NULL;
    thread_t *th = NULL;

	if (mpy_use_spiram) tcb = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	else tcb = malloc(sizeof(StaticTask_t));
    if (tcb == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "can't create thread (tcb)"));
    }

	if (mpy_use_spiram) stack = heap_caps_malloc((stack_size * sizeof(StackType_t))+8, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	else stack = malloc((stack_size * sizeof(StackType_t))+8);;
    if (stack == NULL) {
    	free(tcb);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "can't create thread (stack)"));
    }

	if (mpy_use_spiram) th = heap_caps_malloc(sizeof(thread_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	else th = malloc(sizeof(thread_t));
    if (th == NULL) {
    	free(stack);
    	free(tcb);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "can't create thread (th)"));
    }
    memset(th, 0, sizeof(thread_t));

    mp_thread_mutex_lock(&thread_mutex, 1);

    // adjust the stack_size to provide room to recover from hitting the limit
    //stack_size -= 1024;

    // add thread to linked list of all threads
    th->ready = 0;
    th->arg = arg;
    th->stack = stack;
    th->curr_sp = stack+stack_size;
    th->stack_top = stack+stack_size;
    th->tcb = tcb;
    th->stack_len = stack_size;
    th->next = thread;
    snprintf(th->name, THREAD_NAME_MAX_SIZE, name);
    th->threadQueue = xQueueCreate( THREAD_QUEUE_MAX_ITEMS, sizeof(thread_msg_t) );
    th->allow_suspend = 0;
    th->suspended = 0;
    th->waiting = 0;
    th->deleted = 0;
    th->notifyed = 0;
    if (is_repl) th->type = THREAD_TYPE_REPL;
    else th->type = THREAD_TYPE_PYTHON;
    thread = th;

    // === Create and start the thread task ===
	#if CONFIG_MICROPY_USE_BOTH_CORES
    	TaskHandle_t id = xTaskCreateStatic(freertos_entry, name, stack_size, arg, priority, stack, tcb);
	#else
		int run_on_core = MainTaskCore;
		#if !CONFIG_FREERTOS_UNICORE
		if (!same_core) run_on_core = MainTaskCore ^ 1;
		#endif
		TaskHandle_t id = xTaskCreateStaticPinnedToCore(freertos_entry, name, stack_size, arg, priority, stack, tcb, run_on_core);
	#endif
    if (id == NULL) {
    	// Task not started, restore previous thread and clean-up
    	thread = th->next;
		if (th->threadQueue) vQueueDelete(th->threadQueue);
    	free(th);
    	free(stack);
    	free(tcb);
        mp_thread_mutex_unlock(&thread_mutex);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "can't create thread"));
    }

    th->id = id;
	if (is_repl) ReplTaskHandle = id;

	mp_thread_mutex_unlock(&thread_mutex);
    *in_stack_size = stack_size;
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
		while (n > 0) {
			n = uxQueueMessagesWaiting(th->threadQueue);
			if (n > 0) {
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

//-----------------------------------------
int mp_thread_set_sp(void *sp, void *top) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
			th->curr_sp = sp;
			th->stack_top = top;
			res = 1;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//--------------------------
int mp_thread_get_sp(void) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
			res = (uintptr_t)th->curr_sp;
            break;
        }
    }
    mp_thread_mutex_unlock(&thread_mutex);
    return res;
}

//------------------------------------------------
int mp_thread_set_ptrs(void **ptrs, size_t size) {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id == xTaskGetCurrentTaskHandle()) {
			th->ptrs = ptrs;
			th->ptrs_len = size;
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

//---------------------------
int mp_thread_num_threads() {
	int res = 0;
    mp_thread_mutex_lock(&thread_mutex, 1);
    for (thread_t *th = thread; th != NULL; th = th->next) {
        if (th->id != xTaskGetCurrentTaskHandle()) res++;
    }
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
    		 if ((th->id == ReplTaskHandle) && (accept >= 0)) {
    			 main_accept_msg = accept & 1;
    		 }
			 break;
    	 }
    }
    mp_thread_mutex_unlock(&thread_mutex);

    return res;
}

//------------------------------------------
int mp_thread_mainAcceptMsg(int8_t accept) {
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

// Check if any network connection is available (WiFi STA and/or AP, ethernet)
//-----------------------
static bool _check_network()
{
    uint32_t ip = network_hasip();
    if (ip == 0) {
        #ifdef CONFIG_MICROPY_USE_GSM
        //ToDo: should we enable telnet/ftp over GSM ?
        //if (ppposStatus(NULL, NULL, NULL) != GSM_STATE_CONNECTED) {
            return false;
        //}
        #else
        return false;
        #endif
    }
    return true;
}
#endif

#ifdef CONFIG_MICROPY_USE_TELNET
//===================================
void telnet_task (void *pvParameters)
{
    // Initialize telnet, create rx buffer and mutex
    telnet_init();

    // Check if network connection is available
    while (!_check_network()) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (telnet_stop_requested()) goto exit;
    }

    // We have network connection, enable telnet
    telnet_enable();

    while (1) {
        int res = telnet_run();
        if ( res < 0) {
            if (res == -1) {
                ESP_LOGD("[Telnet]", "\nRun Error");
            }
            // -2 is returned if Telnet stop was requested by user
            break;
        }

        vTaskDelay(1);

        // ---- Check if network is still available ----
        if (!_check_network()) {
            bool was_enabled = telnet_isenabled();
            telnet_disable();
            while (!_check_network()) {
                vTaskDelay(200 / portTICK_PERIOD_MS);
                if (telnet_stop_requested()) goto exit;
            }
            if (was_enabled) telnet_enable();
        }
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
    uint64_t elapsed, time_ms = mp_hal_ticks_ms();
    // Initialize ftp, create rx buffer and mutex
    if (!ftp_init()) {
        ESP_LOGE("[Ftp]", "Init Error");
        goto exit1;
    }

    while (!_check_network()) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (ftp_stop_requested()) goto exit;
    }

    // We have network connection, enable ftp
    ftp_enable();

    time_ms = mp_hal_ticks_ms();
    while (1) {
        // Calculate time between two ftp_run() calls
        elapsed = mp_hal_ticks_ms() - time_ms;
        time_ms = mp_hal_ticks_ms();

        int res = ftp_run(elapsed);
        if (res < 0) {
            if (res == -1) {
                ESP_LOGD("[Ftp]", "\nRun Error");
            }
            // -2 is returned if Ftp stop was requested by user
            break;
        }

        vTaskDelay(1);

        // ---- Check if network is still available ----
        if (!_check_network()) {
            bool was_enabled = ftp_isenabled();
            ftp_disable();
            while (!_check_network()) {
                vTaskDelay(200 / portTICK_PERIOD_MS);
                if (ftp_stop_requested()) goto exit;
            }
            if (was_enabled) ftp_enable();
        }
        // ------------------------------------------
    }
exit:
    ftp_disable();
    ftp_deinit();
exit1:
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
