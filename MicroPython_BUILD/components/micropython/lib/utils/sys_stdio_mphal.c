/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 Damien P. George
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

#include "py/obj.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"

// TODO make stdin, stdout and stderr writable objects so they can
// be changed by Python code.  This requires some changes, as these
// objects are in a read-only module (py/modsys.c).

/******************************************************************************/
// MicroPython bindings

#define STDIO_FD_IN  (0)
#define STDIO_FD_OUT (1)
#define STDIO_FD_ERR (2)

typedef struct _sys_stdio_obj_t {
    mp_obj_base_t base;
    int fd;
} sys_stdio_obj_t;

#if MICROPY_PY_SYS_STDIO_BUFFER
STATIC const sys_stdio_obj_t stdio_buffer_obj;
#endif

void stdio_obj_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    sys_stdio_obj_t *self = self_in;
    mp_printf(print, "<io.FileIO %d>", self->fd);
}

STATIC mp_uint_t stdio_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    sys_stdio_obj_t *self = self_in;
    if (self->fd == STDIO_FD_IN) {
        int c = -1;
        for (uint i = 0; i < size; i++) {
            while (c < 0) {
                c = mp_hal_stdin_rx_chr(1000);
            }
            if (c == '\r') {
                c = '\n';
            }
            ((byte*)buf)[i] = c;
            c = -1;
        }
        return size;
    } else {
        *errcode = MP_EPERM;
        return MP_STREAM_ERROR;
    }
}

STATIC mp_uint_t stdio_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    sys_stdio_obj_t *self = self_in;
    if (self->fd == STDIO_FD_OUT || self->fd == STDIO_FD_ERR) {
        mp_hal_stdout_tx_strn_cooked(buf, size);
        return size;
    } else {
        *errcode = MP_EPERM;
        return MP_STREAM_ERROR;
    }
}

STATIC mp_obj_t stdio_obj___exit__(size_t n_args, const mp_obj_t *args) {
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(stdio_obj___exit___obj, 4, 4, stdio_obj___exit__);

// TODO gc hook to close the file if not already closed

STATIC const mp_rom_map_elem_t stdio_locals_dict_table[] = {
#if MICROPY_PY_SYS_STDIO_BUFFER
    { MP_ROM_QSTR(MP_QSTR_buffer), MP_ROM_PTR(&stdio_buffer_obj) },
#endif
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj)},
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj)},
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&stdio_obj___exit___obj) },
};

STATIC MP_DEFINE_CONST_DICT(stdio_locals_dict, stdio_locals_dict_table);

STATIC const mp_stream_p_t stdio_obj_stream_p = {
    .read = stdio_read,
    .write = stdio_write,
    .is_text = true,
};

STATIC const mp_obj_type_t stdio_obj_type = {
    { &mp_type_type },
    .name = MP_QSTR_FileIO,
    // TODO .make_new?
    .print = stdio_obj_print,
    .getiter = mp_identity_getiter,
    .iternext = mp_stream_unbuffered_iter,
    .protocol = &stdio_obj_stream_p,
    .locals_dict = (mp_obj_dict_t*)&stdio_locals_dict,
};

const sys_stdio_obj_t mp_sys_stdin_obj = {{&stdio_obj_type}, .fd = STDIO_FD_IN};
const sys_stdio_obj_t mp_sys_stdout_obj = {{&stdio_obj_type}, .fd = STDIO_FD_OUT};
const sys_stdio_obj_t mp_sys_stderr_obj = {{&stdio_obj_type}, .fd = STDIO_FD_ERR};

#if MICROPY_PY_SYS_STDIO_BUFFER
STATIC mp_uint_t stdio_buffer_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    for (uint i = 0; i < size; i++) {
        int c = -1;
        while (c < 0) {
            c = mp_hal_stdin_rx_chr(1000);
        }
        ((byte*)buf)[i] = c;
    }
    return size;
}

STATIC mp_uint_t stdio_buffer_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    mp_hal_stdout_tx_strn(buf, size);
    return size;
}

STATIC const mp_stream_p_t stdio_buffer_obj_stream_p = {
    .read = stdio_buffer_read,
    .write = stdio_buffer_write,
    .is_text = false,
};

STATIC const mp_obj_type_t stdio_buffer_obj_type = {
    { &mp_type_type },
    .name = MP_QSTR_FileIO,
    .print = stdio_obj_print,
    .getiter = mp_identity_getiter,
    .iternext = mp_stream_unbuffered_iter,
    .protocol = &stdio_buffer_obj_stream_p,
    .locals_dict = (mp_obj_t)&stdio_locals_dict,
};

STATIC const sys_stdio_obj_t stdio_buffer_obj = {{&stdio_buffer_obj_type}, .fd = 0}; // fd unused
#endif
