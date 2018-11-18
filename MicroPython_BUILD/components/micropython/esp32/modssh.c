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
#include "modnetwork.h"


//-------------------------------------------------------------------------------------------------------
STATIC mp_obj_t curl_SSH_helper(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args, uint8_t type)
{
	network_checkConnection();
    enum { ARG_url, ARG_user, ARG_pass, ARG_key, ARG_file, ARG_port };
	const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,  	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_user, 	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_password,	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_key,      MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_file,     MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_port,     MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = 22 } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int res;
    int hdr_len = ssh2_hdr_maxlen;
    int body_len = ssh2_body_maxlen;
    vstr_t header;
    vstr_t body;
    char *url = NULL;
    char *user = NULL;
    char *pass = NULL;
    char *fname = NULL;
    char *key = NULL;

    char server[128] = {'\0'};
	char fullname[128] = {'\0'};
	char fullkey[128] = {'\0'};
	char scppath[128] = {'\0'};

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

	if (MP_OBJ_IS_STR(args[ARG_key].u_obj)) {
		// Authenticate using a key pair
		key = (char *)mp_obj_str_get_str(args[ARG_key].u_obj);
		res = physicalPath(key, fullkey);
		if ((res != 0) || (strlen(fullkey) == 0)) {
			nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving key file name"));
		}
		key = fullkey;
		if (args[ARG_pass].u_obj != mp_const_none) pass = (char *)mp_obj_str_get_str(args[ARG_pass].u_obj);
	}
	else pass = (char *)mp_obj_str_get_str(args[ARG_pass].u_obj);

	if ((type == 0) || (type == 1)) {
		if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
			// GET/PUT to/from file
			fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
			res = physicalPath(fname, fullname);
			if ((res != 0) || (strlen(fullname) == 0)) {
				nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
			}
			fname = fullname;
			body_len = MIN_BODY_BUF_LEN;
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
	res = ssh_SCP(type, server, sport, scppath, user, pass, key, fname, header.buf, body.buf, hdr_len, body_len);
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

//--------------------------------------------------------------------------------------
STATIC mp_obj_t SSH2_Options(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_print, ARG_verbose, ARG_progress, ARG_timeout, ARG_maxfsize, ARG_hdrlen, ARG_bodylen, ARG_trace };
	const mp_arg_t allowed_args[] = {
		{ MP_QSTR_print,                      MP_ARG_BOOL, { .u_bool = true } },
		{ MP_QSTR_verbose,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_progress, MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_timeout,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_maxfsize, MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_hdrlen,   MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_bodylen,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_trace,    MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
	};

	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_verbose].u_int >= 0) ssh2_verbose = args[ARG_verbose].u_int & 1;
    if (args[ARG_progress].u_int >= 0) ssh2_progress = args[ARG_progress].u_int;
    if (args[ARG_timeout].u_int >= 10) ssh2_session_timeout = args[ARG_timeout].u_int;
    if (args[ARG_maxfsize].u_int > 1000) ssh2_maxbytes = args[ARG_maxfsize].u_int;
    if (args[ARG_hdrlen].u_int > MIN_HDR_BUF_LEN) ssh2_hdr_maxlen = args[ARG_hdrlen].u_int;
    if (args[ARG_bodylen].u_int > MIN_BODY_BUF_LEN) ssh2_body_maxlen = args[ARG_bodylen].u_int;
    if (args[ARG_trace].u_int >= 0) ssh2_session_trace = args[ARG_trace].u_int & 0x3FF;

	if (ssh2_session_timeout > CONFIG_TASK_WDT_TIMEOUT_S) ssh2_session_timeout = CONFIG_TASK_WDT_TIMEOUT_S - 2;
	if (ssh2_session_timeout < 2) ssh2_session_timeout = 2;
	if (ssh2_session_timeout > 30) ssh2_session_timeout = 30;

	if (args[ARG_print].u_bool) {
        mp_printf(&mp_plat_print, "SSH options(\n  Verbose: %s, Progress: %d, Timeout: %d s, Max fsize: %d, Header len: %d, Body len: %d, Trace: %s (0x%03X)\n)\n",
        		((ssh2_verbose) ? "True" : "False"), ssh2_progress, ssh2_session_timeout, ssh2_maxbytes, ssh2_hdr_maxlen, ssh2_body_maxlen, ((ssh2_session_trace) ? "True" : "False"), ssh2_session_trace);

    }
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(SSH2_Options_obj, 0, SSH2_Options);


/*
 *  Trace constants
	LIBSSH2_TRACE_TRANS (1<<1)
	LIBSSH2_TRACE_KEX   (1<<2)
	LIBSSH2_TRACE_AUTH  (1<<3)
	LIBSSH2_TRACE_CONN  (1<<4)
	LIBSSH2_TRACE_SCP   (1<<5)
	LIBSSH2_TRACE_SFTP  (1<<6)
	LIBSSH2_TRACE_ERROR (1<<7)
	LIBSSH2_TRACE_PUBLICKEY (1<<8)
	LIBSSH2_TRACE_SOCKET (1<<9)
*/

//===========================================================
STATIC const mp_rom_map_elem_t ssh_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ssh) },

    { MP_ROM_QSTR(MP_QSTR_get),				MP_ROM_PTR(&SSH2_GET_obj) },
    { MP_ROM_QSTR(MP_QSTR_put),				MP_ROM_PTR(&SSH2_PUT_obj) },
    { MP_ROM_QSTR(MP_QSTR_list),			MP_ROM_PTR(&SSH2_LIST_obj) },
    { MP_ROM_QSTR(MP_QSTR_llist),			MP_ROM_PTR(&SSH2_LLIST_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir),			MP_ROM_PTR(&SSH2_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_exec),			MP_ROM_PTR(&SSH2_exec_obj) },
    { MP_ROM_QSTR(MP_QSTR_options),			MP_ROM_PTR(&SSH2_Options_obj) },

	// Constants
	{ MP_ROM_QSTR(MP_QSTR_TRACE_TRANS),		MP_ROM_INT(LIBSSH2_TRACE_TRANS) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_KEX),		MP_ROM_INT(LIBSSH2_TRACE_KEX) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_AUTH),		MP_ROM_INT(LIBSSH2_TRACE_AUTH) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_CONN),		MP_ROM_INT(LIBSSH2_TRACE_CONN) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_SCP),		MP_ROM_INT(LIBSSH2_TRACE_SCP) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_SFTP),		MP_ROM_INT(LIBSSH2_TRACE_SFTP) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_ERROR),		MP_ROM_INT(LIBSSH2_TRACE_ERROR) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_PUBLICKEY),	MP_ROM_INT(LIBSSH2_TRACE_PUBLICKEY) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_SOCKET),	MP_ROM_INT(LIBSSH2_TRACE_SOCKET) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_NONE),		MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_TRACE_ALL),		MP_ROM_INT(0x3FE) },
};
STATIC MP_DEFINE_CONST_DICT(ssh_module_globals, ssh_module_globals_table);

//======================================
const mp_obj_module_t mp_module_ssh = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&ssh_module_globals,
};

#endif

