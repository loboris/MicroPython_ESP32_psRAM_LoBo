/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Damien P. George
 * Copyright (c) 2016-2017 Paul Sokolovsky
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

#include <string.h>
#include <math.h>
#include "py/objlist.h"
#include "py/runtime.h"
#include "py/smallint.h"

#if MICROPY_PY_UTIMEQ

struct qentry {
    int64_t time;
    mp_uint_t id;
    mp_obj_t callback;
    mp_obj_t args;
};

typedef struct _mp_obj_utimeq_t {
    mp_obj_base_t base;
    mp_uint_t alloc;
    mp_uint_t len;
    bool ascending;
    struct qentry items[];
} mp_obj_utimeq_t;

STATIC mp_uint_t utimeq_id = 0;
STATIC bool sort_asc = true;

//--------------------------------------------------
STATIC mp_obj_utimeq_t *get_heap(mp_obj_t heap_in) {
    return MP_OBJ_TO_PTR(heap_in);
}

//----------------------------------------------------------------
STATIC int compare_times(const void * item, const void * parent) {
    int ret = 0;
    int64_t res = ((struct qentry *)parent)->time - ((struct qentry *)item)->time;
	ret = (res < 0) ? -1 : 1;
    if (res == 0) {
    	ret = (((struct qentry *)parent)->id - ((struct qentry *)item)->id);
    }
	if (sort_asc) ret *= -1;

	return ret;
}

//---------------------------------------------
STATIC void sort_items(mp_obj_utimeq_t *heap) {
	sort_asc = heap->ascending;
    qsort(&heap->items, heap->len, sizeof(struct qentry), compare_times);
}

//----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t utimeq_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_size, ARG_sort };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_size,	MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none}},
        { MP_QSTR_asc,	MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = true}},
    };
    // parse arguments
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_arg_check_num(n_args, n_kw, 1, 1, true);

    mp_uint_t alloc = mp_obj_get_int(args[ARG_size].u_obj);
    mp_obj_utimeq_t *o = m_new_obj_var(mp_obj_utimeq_t, struct qentry, alloc);
    o->base.type = type;
    memset(o->items, 0, sizeof(*o->items) * alloc);
    o->alloc = alloc;
    o->len = 0;
    o->ascending = args[ARG_sort].u_bool;

    return MP_OBJ_FROM_PTR(o);
}

//------------------------------------------------------------------------
STATIC mp_obj_t mod_utimeq_heappush(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_obj_t heap_in = args[0];
    mp_obj_utimeq_t *heap = get_heap(heap_in);
    if (heap->len == heap->alloc) {
        mp_raise_msg(&mp_type_IndexError, "queue overflow");
    }
    mp_uint_t l = heap->len;
    // time argument can be float or integer
    // if float, convert it to 64-bit integer
    int64_t itime;
    if (mp_obj_is_float(args[1])) {
    	mp_float_t time = mp_obj_float_get(args[1]);
        itime = (int64_t)(round(time));
    }
    else itime = mp_obj_get_int64(args[1]);

    heap->items[l].time = itime;
    heap->items[l].id = utimeq_id++;
    heap->items[l].callback = args[2];
    heap->items[l].args = args[3];
    heap->len++;

    sort_items(heap);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_utimeq_heappush_obj, 4, 4, mod_utimeq_heappush);

//-----------------------------------------------------------------------
STATIC mp_obj_t mod_utimeq_heappop(mp_obj_t heap_in, mp_obj_t list_ref) {
    mp_obj_utimeq_t *heap = get_heap(heap_in);
    if (heap->len == 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_IndexError, "empty heap"));
    }
    mp_obj_list_t *ret = MP_OBJ_TO_PTR(list_ref);
    if (!MP_OBJ_IS_TYPE(list_ref, &mp_type_list) || ret->len < 3) {
        mp_raise_TypeError(NULL);
    }

    struct qentry *item = &heap->items[0];
    ret->items[0] = mp_obj_new_int_from_ll(item->time);
    ret->items[1] = item->callback;
    ret->items[2] = item->args;

    heap->len -= 1;

    if (heap->len) {
    	memmove(&heap->items[0], &heap->items[1], sizeof(struct qentry) * heap->len);
        //sort_items(heap);
        // we don't want to retain a pointers !
        memset(&heap->items[heap->len], 0, sizeof(struct qentry));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mod_utimeq_heappop_obj, mod_utimeq_heappop);

//-----------------------------------------------------------------------------------------
STATIC mp_obj_t mod_utimeq_heappeek(mp_obj_t heap_in, mp_obj_t idx_in, mp_obj_t list_ref) {
    mp_obj_utimeq_t *heap = get_heap(heap_in);
    if (heap->len == 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_IndexError, "empty heap"));
    }
	int pos = mp_obj_get_int(idx_in);
	if ((pos < 0) || (pos >= heap->len)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_IndexError, "wrong heap index"));
	}
    mp_obj_list_t *ret = MP_OBJ_TO_PTR(list_ref);
    if (!MP_OBJ_IS_TYPE(list_ref, &mp_type_list) || ret->len < 3) {
        mp_raise_TypeError(NULL);
    }

    struct qentry *item = &heap->items[pos];
    ret->items[0] = mp_obj_new_int_from_ll(item->time);
    ret->items[1] = item->callback;
    ret->items[2] = item->args;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(mod_utimeq_heappeek_obj, mod_utimeq_heappeek);

//------------------------------------------------------------------------
STATIC mp_obj_t mod_utimeq_peektime(size_t n_args, const mp_obj_t *args) {
    mp_obj_utimeq_t *heap = get_heap(args[0]);
    if (heap->len == 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_IndexError, "empty heap"));
    }

    int pos = 0;
    if (n_args == 2) {
    	pos = mp_obj_get_int(args[1]);
    	if ((pos < 0) || (pos >= heap->len)) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_IndexError, "wrong heap index"));
    	}
    }
    struct qentry *item = &heap->items[pos];
    return mp_obj_new_int_from_ll(item->time);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_utimeq_peektime_obj, 1, 2, mod_utimeq_peektime);

//------------------------------------------------
STATIC mp_obj_t mod_utimeq_len(mp_obj_t heap_in) {
    mp_obj_utimeq_t *heap = get_heap(heap_in);
    mp_obj_tuple_t *t = mp_obj_new_tuple(2, NULL);

    t->items[0] = mp_obj_new_int(heap->len);
	t->items[1] = mp_obj_new_int(heap->alloc);

    return t;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_utimeq_len_obj, mod_utimeq_len);

//--------------------------------------------------------------------
STATIC mp_obj_t mod_utimeq_dump(size_t n_args, const mp_obj_t *args) {
    mp_obj_utimeq_t *heap = get_heap(args[0]);

    int maxlen = heap->len;
    if (n_args == 2) {
    	if ( mp_obj_is_true(args[1])) maxlen = heap->alloc;
    }
    printf("%4s%21s%10s%12s%12s\n", "Idx", "Time", "ID", "Callback", "Arg");
    printf("-----------------------------------------------------------\n");
    for (int i = 0; i < maxlen; i++) {
        printf("%4d%21lld%10u%12p%12p", i, heap->items[i].time, heap->items[i].id,
            MP_OBJ_TO_PTR(heap->items[i].callback), MP_OBJ_TO_PTR(heap->items[i].args));
        if (i >= heap->len) printf("  (empty)\n");
        else printf("\n");
    }
    printf("-----------------------------------------------------------\n");
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_utimeq_dump_obj, 1, 2, mod_utimeq_dump);

//-------------------------------------------------------------------
STATIC mp_obj_t utimeq_unary_op(mp_unary_op_t op, mp_obj_t self_in) {
    mp_obj_utimeq_t *self = MP_OBJ_TO_PTR(self_in);
    switch (op) {
        case MP_UNARY_OP_BOOL: return mp_obj_new_bool(self->len != 0);
        case MP_UNARY_OP_LEN: return MP_OBJ_NEW_SMALL_INT(self->len);
        default: return MP_OBJ_NULL; // op not supported
    }
}

//===========================================================
STATIC const mp_rom_map_elem_t utimeq_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_push),     MP_ROM_PTR(&mod_utimeq_heappush_obj) },
    { MP_ROM_QSTR(MP_QSTR_pop),      MP_ROM_PTR(&mod_utimeq_heappop_obj) },
    { MP_ROM_QSTR(MP_QSTR_peek),     MP_ROM_PTR(&mod_utimeq_heappeek_obj) },
    { MP_ROM_QSTR(MP_QSTR_peektime), MP_ROM_PTR(&mod_utimeq_peektime_obj) },
    { MP_ROM_QSTR(MP_QSTR_len),      MP_ROM_PTR(&mod_utimeq_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_dump),     MP_ROM_PTR(&mod_utimeq_dump_obj) },
};
STATIC MP_DEFINE_CONST_DICT(utimeq_locals_dict, utimeq_locals_dict_table);

//========================================
STATIC const mp_obj_type_t utimeq_type = {
    { &mp_type_type },
    .name = MP_QSTR_utimeq,
    .make_new = utimeq_make_new,
    .unary_op = utimeq_unary_op,
    .locals_dict = (void*)&utimeq_locals_dict,
};

//=================================================================
STATIC const mp_rom_map_elem_t mp_module_utimeq_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_utimeq) },
    { MP_ROM_QSTR(MP_QSTR_utimeq),   MP_ROM_PTR(&utimeq_type) },
};
STATIC MP_DEFINE_CONST_DICT(mp_module_utimeq_globals, mp_module_utimeq_globals_table);

//========================================
const mp_obj_module_t mp_module_utimeq = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_utimeq_globals,
};

#endif //MICROPY_PY_UTIMEQ
