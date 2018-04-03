/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Damien P. George
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

#include "py/runtime.h"

#if MICROPY_ENABLE_SCHEDULER

// A variant of this is inlined in the VM at the pending exception check
void mp_handle_pending(void) {
    if (MP_STATE_VM(sched_state) == MP_SCHED_PENDING) {
        mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
        mp_obj_t obj = MP_STATE_VM(mp_pending_exception);
        if (obj != MP_OBJ_NULL) {
            MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
            if (!mp_sched_num_pending()) {
                MP_STATE_VM(sched_state) = MP_SCHED_IDLE;
            }
            MICROPY_END_ATOMIC_SECTION(atomic_state);
            nlr_raise(obj);
        }
        mp_handle_pending_tail(atomic_state);
    }
}

//-----------------------------------
void free_carg(mp_sched_carg_t *carg)
{
	for (int i=0; i<MP_SCHED_CTYPE_MAX_ITEMS; i++) {
		if (carg->entry[i]) {
			if (carg->type == MP_SCHED_ENTRY_TYPE_CARG) {
				free_carg((mp_sched_carg_t *)carg->entry[i]);
			}
			else {
				mp_sched_carg_entry_t *entry = (mp_sched_carg_entry_t *)carg->entry[i];
				if (entry->sval) {
					free(entry->sval);
					entry->sval = NULL;
				}
			}
			free(carg->entry[i]);
		}
	}
	free(carg);
	carg = NULL;
}

//---------------------------------------------------------------------------------------------------------------------------
mp_sched_carg_t *make_carg_entry(mp_sched_carg_t *carg, int idx, uint8_t type, int val, const uint8_t *sval, const char *key)
{
	carg->entry[idx] = calloc(sizeof(mp_sched_carg_entry_t), 1);
	if (carg->entry[idx] == NULL) {
		free_carg(carg);
		return NULL;
	}

	mp_sched_carg_entry_t *entry = (mp_sched_carg_entry_t *)carg->entry[idx];

	entry->type = type;
	if (key) sprintf(entry->key, key);

	if (sval) {
		entry->ival = val;
		entry->sval = malloc(val);
		if (entry->sval == NULL) {
			free_carg(carg);
			return NULL;
		}
		memcpy(entry->sval, sval, val);
	}
	else {
		entry->ival = val;
	}
	carg->n++;
	return carg;
}

//------------------------------------------------------------------------------------------
mp_sched_carg_t *make_carg_entry_carg(mp_sched_carg_t *carg, int idx, mp_sched_carg_t *darg)
{
	carg->entry[idx] = calloc(sizeof(mp_sched_carg_entry_t), 1);
	if (carg->entry[idx] == NULL) {
		free_carg(carg);
		return NULL;
	}

	mp_sched_carg_entry_t *entry = (mp_sched_carg_entry_t *)carg->entry[idx];
	entry->type = MP_SCHED_ENTRY_TYPE_CARG;
	entry->carg = darg;
	carg->n++;
	return carg;
}

//-----------------------------------
mp_sched_carg_t *make_cargs(int type)
{
	// Create scheduler function arguments
	mp_sched_carg_t *carg = calloc(sizeof(mp_sched_carg_t), 1);
	if (carg == NULL) return NULL;

	carg->type = type;
	carg->n = 0;
	return carg;
}

//------------------------------------------------------------------
static mp_obj_t make_arg_from_carg(mp_sched_carg_t *carg, int level)
{
    mp_obj_t arg = mp_const_none;
	if (carg->type == MP_SCHED_CTYPE_DICT) {
		//dictionary
		mp_obj_dict_t *dct = mp_obj_new_dict(0);
		for (int i = 0; i < carg->n; i++) {
			mp_sched_carg_entry_t *entry = (mp_sched_carg_entry_t *)carg->entry[i];
			if (entry->type == MP_SCHED_ENTRY_TYPE_INT) {
				mp_obj_dict_store(dct, mp_obj_new_str(entry->key, strlen(entry->key)),
						mp_obj_new_int(entry->ival));
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_BOOL) {
				mp_obj_dict_store(dct, mp_obj_new_str(entry->key, strlen(entry->key)),
						mp_obj_new_bool(entry->ival));
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_FLOAT) {
				mp_obj_dict_store(dct, mp_obj_new_str(entry->key, strlen(entry->key)),
						mp_obj_new_float(entry->fval));
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_STR) {
				mp_obj_dict_store(dct, mp_obj_new_str(entry->key, strlen(entry->key)),
						mp_obj_new_str((const char*)entry->sval, entry->ival));
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_BYTES) {
				mp_obj_dict_store(dct, mp_obj_new_str(entry->key, strlen(entry->key)),
						mp_obj_new_bytes((const byte*)entry->sval, entry->ival));
			}
			else if ((level == 0) && (entry->type == MP_SCHED_ENTRY_TYPE_CARG) && (strlen(entry->key) > 0) && (entry->carg)) {
				mp_obj_t darg = make_arg_from_carg(entry->carg, 1);
				mp_obj_dict_store(dct, mp_obj_new_str(entry->key, strlen(entry->key)), darg);
			}
		}
		arg = dct;
	}
	else if (carg->type == MP_SCHED_CTYPE_TUPLE) {
		//tuple
		mp_obj_t tuple[carg->n];
		for (int i = 0; i < carg->n; i++) {
			mp_sched_carg_entry_t *entry = (mp_sched_carg_entry_t *)carg->entry[i];
			if (entry->type == MP_SCHED_ENTRY_TYPE_INT) {
				tuple[i] = mp_obj_new_int(entry->ival);
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_BOOL) {
				tuple[i] = mp_obj_new_bool(entry->ival);
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_FLOAT) {
				tuple[i] = mp_obj_new_float(entry->fval);
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_STR) {
				tuple[i] = mp_obj_new_str((const char*)entry->sval, entry->ival);
			}
			else if (entry->type == MP_SCHED_ENTRY_TYPE_BYTES) {
				tuple[i] = mp_obj_new_bytes((const byte*)entry->sval, entry->ival);
			}
			else if ((level == 0) && (entry->type == MP_SCHED_ENTRY_TYPE_CARG) && (entry->carg)) {
				tuple[i] = make_arg_from_carg(entry->carg, 1);
			}
			else tuple[i] = mp_const_none;
		}
		arg = mp_obj_new_tuple(carg->n, tuple);
	}
	else {
		// Simple type, single entry
		mp_sched_carg_entry_t *entry = (mp_sched_carg_entry_t *)carg->entry[0];
		if (entry->type == MP_SCHED_ENTRY_TYPE_INT) {
			arg = mp_obj_new_int(entry->ival);
		}
		else if (entry->type == MP_SCHED_ENTRY_TYPE_BOOL) {
			arg = mp_obj_new_bool(entry->ival);
		}
		else if (entry->type == MP_SCHED_ENTRY_TYPE_FLOAT) {
			arg = mp_obj_new_float(entry->fval);
		}
		else if (entry->type == MP_SCHED_ENTRY_TYPE_STR) {
			arg = mp_obj_new_str((const char*)entry->sval, entry->ival);
		}
		else if (entry->type == MP_SCHED_ENTRY_TYPE_BYTES) {
			arg = mp_obj_new_bytes((const byte*)entry->sval, entry->ival);
		}
	}
	free_carg(carg);
	return arg;
}

// This function should only be called be mp_sched_handle_pending,
// or by the VM's inlined version of that function.
//---------------------------------------------------
void mp_handle_pending_tail(mp_uint_t atomic_state) {
    MP_STATE_VM(sched_state) = MP_SCHED_LOCKED;
    if (MP_STATE_VM(sched_sp) > 0) {
    	// Get scheduled item, decrease stack counter
        //mp_sched_item_t item = MP_STATE_VM(sched_stack)[--MP_STATE_VM(sched_sp)];

        // get the first scheduled item from stack
        mp_sched_item_t item = MP_STATE_VM(sched_stack)[0];
        // Move other items
        MP_STATE_VM(sched_sp)--;
        int i = 0;
        while (i < MP_STATE_VM(sched_sp)) {
        	memcpy(&(MP_STATE_VM(sched_stack)[i]), &(MP_STATE_VM(sched_stack)[i+1]), sizeof(mp_sched_item_t));
        	i++;
        }

        mp_obj_t arg = mp_const_none;
        if (item.carg != NULL) {
        	// === C argument is present, create the MicroPython object argument from it ===
        	arg = make_arg_from_carg((mp_sched_carg_t *)item.carg, 0);
        }
        else arg = item.arg;
        MICROPY_END_ATOMIC_SECTION(atomic_state);
        mp_call_function_1_protected(item.func, arg);
    } else {
        MICROPY_END_ATOMIC_SECTION(atomic_state);
    }
    mp_sched_unlock();
}

//------------------------
void mp_sched_lock(void) {
    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
    if (MP_STATE_VM(sched_state) < 0) {
        --MP_STATE_VM(sched_state);
    } else {
        MP_STATE_VM(sched_state) = MP_SCHED_LOCKED;
    }
    MICROPY_END_ATOMIC_SECTION(atomic_state);
}

//--------------------------
void mp_sched_unlock(void) {
    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
    if (++MP_STATE_VM(sched_state) == 0) {
        // vm became unlocked
        if (MP_STATE_VM(mp_pending_exception) != MP_OBJ_NULL || mp_sched_num_pending()) {
            MP_STATE_VM(sched_state) = MP_SCHED_PENDING;
        } else {
            MP_STATE_VM(sched_state) = MP_SCHED_IDLE;
        }
    }
    MICROPY_END_ATOMIC_SECTION(atomic_state);
}

//-------------------------------------------------------------------
bool mp_sched_schedule(mp_obj_t function, mp_obj_t arg, void *carg) {
    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
    bool ret;
    if (MP_STATE_VM(sched_sp) < MICROPY_SCHEDULER_DEPTH) {
        if (MP_STATE_VM(sched_state) == MP_SCHED_IDLE) {
            MP_STATE_VM(sched_state) = MP_SCHED_PENDING;
        }
        MP_STATE_VM(sched_stack)[MP_STATE_VM(sched_sp)].func = function;
        MP_STATE_VM(sched_stack)[MP_STATE_VM(sched_sp)].arg = arg;
        MP_STATE_VM(sched_stack)[MP_STATE_VM(sched_sp)].carg = carg;
        ++MP_STATE_VM(sched_sp);
        ret = true;
    } else {
        // schedule stack is full
        ret = false;
    }
    MICROPY_END_ATOMIC_SECTION(atomic_state);
    return ret;
}

#else // MICROPY_ENABLE_SCHEDULER

// A variant of this is inlined in the VM at the pending exception check
//----------------------------
void mp_handle_pending(void) {
    if (MP_STATE_VM(mp_pending_exception) != MP_OBJ_NULL) {
        mp_obj_t obj = MP_STATE_VM(mp_pending_exception);
        MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
        nlr_raise(obj);
    }
}

#endif // MICROPY_ENABLE_SCHEDULER
