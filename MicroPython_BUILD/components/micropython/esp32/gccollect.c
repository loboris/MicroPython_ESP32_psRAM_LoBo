/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Damien P. George
 * Copyright (c) 2017 Pycom Limited
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

#include "py/mpconfig.h"
#include "py/mpstate.h"
#include "py/gc.h"
#include "py/mpthread.h"
#include "gccollect.h"
#include "soc/cpu.h"
#include "xtensa/hal.h"

static int n_marked;

static void gc_collect_inner(int level, int flag)
{
    if (level < XCHAL_NUM_AREGS / 8) {
        gc_collect_inner(level + 1, flag);
        if (level != 0) {
            return;
        }
    }

    if (level == XCHAL_NUM_AREGS / 8) {
		n_marked = MP_STATE_MEM(gc_marked);

        // get the stack pointer
        volatile uint32_t sp = (uint32_t)get_sp();
		gc_collect_root((void**)sp, ((mp_uint_t)MP_STATE_THREAD(stack_top) - sp) / sizeof(uint32_t));

		if ((flag) && ((MP_STATE_MEM(gc_marked) - n_marked) > 0)) printf("gc_collect:  marked on stack: %d (%p - %p)\n", MP_STATE_MEM(gc_marked) - n_marked, (void *)sp, (void *)MP_STATE_THREAD(stack_top));
        return;
    }

    // Trace root pointers from other threads
	int n_marked = MP_STATE_MEM(gc_marked);

	mp_thread_gc_others(flag);

	if ((flag) && ((MP_STATE_MEM(gc_marked) - n_marked) > 0)) printf("gc_collect: marked on others: %d\n", MP_STATE_MEM(gc_marked) - n_marked);
}

//-----------------------
void gc_collect(int flag)
{
	char *th_name = NULL;
	if (flag) {
		char thname[THREAD_NAME_MAX_SIZE+1];
		th_name = thname;
		mp_thread_getSelfname(th_name);
	}
	if (flag > 1) gc_dump_alloc_table();

	// Trace root pointers.
	gc_collect_start();
	if (flag) printf("gc_collect:  marked on START: %d (th='%s')\n", MP_STATE_MEM(gc_marked), th_name);

	// ---- Collect inner -------------------------------------------------------
	gc_collect_inner(0, flag);

    if (flag > 1) {
		mp_thread_mutex_unlock(&(mp_state_ctx.mem.gc_mutex));
		gc_dump_alloc_table();
		mp_thread_mutex_lock(&(mp_state_ctx.mem.gc_mutex), 1);
    }

	n_marked = MP_STATE_MEM(gc_marked);

	gc_collect_end();

	if (flag) {
		if ((MP_STATE_MEM(gc_marked) - n_marked) > 0) printf("gc_collect:    marked on end: %d\n", MP_STATE_MEM(gc_marked) - n_marked);
		printf("gc_collect:     marked total: %d; collected: %d\n", MP_STATE_MEM(gc_marked), MP_STATE_MEM(gc_collected));
	}
	if (flag > 1) gc_dump_alloc_table();
}
