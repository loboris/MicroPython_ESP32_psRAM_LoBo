/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2015 Damien P. George
 * Copyright (c) 2016 Paul Sokolovsky
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

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_SSH

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"
#include "esp_system.h"

#include "py/obj.h"
#include "py/runtime.h"

#include "libs/espcurl.h"
#include "extmod/vfs_native.h"
#include "libssh2.h"

//-------------------------------------------------------------------------------------------------------
STATIC mp_obj_t curl_SSH_helper(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args, uint8_t type)
{
	checkConnection();
    enum { ARG_url, ARG_user, ARG_pass, ARG_file, ARG_port };
	const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,  	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_user, 	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_password,	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_file,     	              MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_port,         	          MP_ARG_INT, { .u_int = 22 } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int res;
    int hdr_len = hdr_maxlen;
    int body_len = body_maxlen;
    vstr_t header;
    vstr_t body;
    char *url = NULL;
    char *user = NULL;
    char *pass = NULL;
    char *fname = NULL;

    char server[128] = {'\0'};
	char fullname[128] = {'\0'};
	char scppath[128] = {'\0'};

	/*uint8_t type = (uint8_t)args[ARG_type].u_int;
    if (type > 4) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Unsupported operation type"));
    }*/

    uint16_t port = (uint16_t)args[ARG_port].u_int;
    char sport[8] = {'\0'};
    sprintf(sport,"%u", port);

	url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);
	if (strstr(url, "//") != NULL) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "URL must not contain '//'"));
	}
	char *fpath_pos = strchr(url, '/');
	if ((fpath_pos == NULL) || ((fpath_pos-url) < 4)) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "File path cannot be resolved"));
	}
	memcpy(server, url, fpath_pos-url);
	server[fpath_pos-url] = '\0';
	strcpy(scppath, fpath_pos);
	if (type == 5) {
		if (scppath[0] == '/') memmove(scppath, scppath+1, strlen(scppath));
	}

	user = (char *)mp_obj_str_get_str(args[ARG_user].u_obj);
	pass = (char *)mp_obj_str_get_str(args[ARG_pass].u_obj);

	if ((type == 0) || (type == 1)) {
		if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
			// GET/PUT to/from file
			fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
			res = physicalPath(fname, fullname);
			if ((res != 0) || (strlen(fullname) == 0)) {
				nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
			}
			fname = fullname;
			body_len = MIN_HDR_BODY_BUF_LEN;
		}
		else if (type == 1) {
			nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Expected file name for upload"));
		}
	}

    vstr_init_len(&header, hdr_len);
    vstr_init_len(&body, body_len);
    header.buf[0] = '\0';
    body.buf[0] = '\0';

   	MP_THREAD_GIL_EXIT();
	res = ssh_SCP(type, server, sport, scppath, user, pass, fname, header.buf, body.buf, hdr_len, body_len);
   	MP_THREAD_GIL_ENTER();

   	mp_obj_t tuple[3];
	tuple[0] = mp_obj_new_int(res);
	header.len = strlen(header.buf);
	body.len = strlen(body.buf);
	tuple[1] = mp_obj_new_str_from_vstr(&mp_type_str, &header);
	tuple[2] = mp_obj_new_str_from_vstr(&mp_type_str, &body);

   	return mp_obj_new_tuple(3, tuple);
}

//--------------------------------------------------------------------------------------
STATIC mp_obj_t SSH2_GET(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_SSH_helper(n_args, pos_args, kw_args, 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(SSH2_GET_obj, 3, SSH2_GET);

//--------------------------------------------------------------------------------------
STATIC mp_obj_t SSH2_PUT(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_SSH_helper(n_args, pos_args, kw_args, 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(SSH2_PUT_obj, 3, SSH2_PUT);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t SSH2_LIST(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_SSH_helper(n_args, pos_args, kw_args, 2);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(SSH2_LIST_obj, 3, SSH2_LIST);

//----------------------------------------------------------------------------------------
STATIC mp_obj_t SSH2_LLIST(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_SSH_helper(n_args, pos_args, kw_args, 3);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(SSH2_LLIST_obj, 3, SSH2_LLIST);

//----------------------------------------------------------------------------------------
STATIC mp_obj_t SSH2_mkdir(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_SSH_helper(n_args, pos_args, kw_args, 4);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(SSH2_mkdir_obj, 3, SSH2_mkdir);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t SSH2_exec(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_SSH_helper(n_args, pos_args, kw_args, 5);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(SSH2_exec_obj, 3, SSH2_exec);



//============================================================
STATIC const mp_rom_map_elem_t curl_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ssh) },

    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&SSH2_GET_obj) },
    { MP_ROM_QSTR(MP_QSTR_put), MP_ROM_PTR(&SSH2_PUT_obj) },
    { MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&SSH2_LIST_obj) },
    { MP_ROM_QSTR(MP_QSTR_llist), MP_ROM_PTR(&SSH2_LLIST_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&SSH2_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_exec), MP_ROM_PTR(&SSH2_exec_obj) },
};
STATIC MP_DEFINE_CONST_DICT(curl_module_globals, curl_module_globals_table);

//======================================
const mp_obj_module_t mp_module_ssh = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&curl_module_globals,
};

#endif

