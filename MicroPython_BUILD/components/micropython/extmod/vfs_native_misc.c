/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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
 * Originally created by SHA2017 Badge Team (https://github.com/SHA2017-badge/micropython-esp32)
 *
 * Modified by LoBo (https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo)
 *
 * Added support for SD Card and some changes to make it work (hopefully) better
 *
 */

#include "py/mpconfig.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>

#include "py/nlr.h"
#include "py/runtime.h"
#include "extmod/vfs_native.h"
#include "py/lexer.h"

//static const char *TAG = "vfs_native_misc";

typedef struct _mp_vfs_native_ilistdir_it_t {
	mp_obj_base_t base;
	mp_fun_1_t iternext;
	bool is_str;
	DIR *dir;
} mp_vfs_native_ilistdir_it_t;

//--------------------------------------------------------------------
STATIC mp_obj_t mp_vfs_native_ilistdir_it_iternext(mp_obj_t self_in) {
	mp_vfs_native_ilistdir_it_t *self = MP_OBJ_TO_PTR(self_in);

	for (;;) {
		struct dirent *de;
		de = readdir(self->dir);
		if (de == NULL) {
			// stop on error or end of dir
			break;
		}

		char *fn = de->d_name;

		// filter . and ..
		if (fn[0] == '.' && ((fn[1] == '.' && fn[2] == 0) || fn[1] == 0))
			continue;

		// make 3-tuple with info about this entry
		mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(3, NULL));
		if (self->is_str) {
			t->items[0] = mp_obj_new_str(fn, strlen(fn));
		} else {
			t->items[0] = mp_obj_new_bytes((const byte*)fn, strlen(fn));
		}
		if (de->d_type & DT_DIR) {
			// dir
			t->items[1] = MP_OBJ_NEW_SMALL_INT(MP_S_IFDIR);
		} else {
			// file
			t->items[1] = MP_OBJ_NEW_SMALL_INT(MP_S_IFREG);
		}
		t->items[2] = MP_OBJ_NEW_SMALL_INT(0); // no inode number

		return MP_OBJ_FROM_PTR(t);
	}

	// ignore error because we may be closing a second time
	closedir(self->dir);

	return MP_OBJ_STOP_ITERATION;
}

//---------------------------------------------------------------------------------------
mp_obj_t native_vfs_ilistdir2(fs_user_mount_t *vfs, const char *path, bool is_str_type) {
	mp_vfs_native_ilistdir_it_t *iter = m_new_obj(mp_vfs_native_ilistdir_it_t);
	iter->base.type = &mp_type_polymorph_iter;
	iter->iternext = mp_vfs_native_ilistdir_it_iternext;
	iter->is_str = is_str_type;

	DIR *d = opendir(path);
	if (d == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}
	iter->dir = d;
	return MP_OBJ_FROM_PTR(iter);
}

//-------------------------------------------------------------------------------
mp_import_stat_t native_vfs_import_stat(fs_user_mount_t *vfs, const char *path) {
	char absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	path = mkabspath(vfs, path, absbuf, sizeof(absbuf));
	if (path == NULL) {
		return MP_IMPORT_STAT_NO_EXIST;
	}

	struct stat buf;
	int res = stat(path, &buf);
	if (res < 0) {
		return MP_IMPORT_STAT_NO_EXIST;
	}
	if ((buf.st_mode & S_IFDIR) == 0) {
		return MP_IMPORT_STAT_FILE;
	}
	return MP_IMPORT_STAT_DIR;
}

