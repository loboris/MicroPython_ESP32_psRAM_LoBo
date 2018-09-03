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

#ifdef CONFIG_MICROPY_USE_CURL

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
#include "modmachine.h"
#include "modnetwork.h"
#include "libs/espcurl.h"
#include "libs/curl_mail.h"
#include "extmod/vfs_native.h"


//----------------------------------
static int check_file(char *fname) {
	struct stat sb;
	if ((stat(fname, &sb) == 0) && (sb.st_size > 0)) {
		return 1;
	}
	return 0;
}

// Print some info about curl environment
//--------------------------------------------------
static void print_curl_info(const mp_print_t *print)
{
	curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);

    mp_printf(print, "Curl Info (\n");

    mp_printf(print, "  Curl version info\n");
    mp_printf(print, "    version: %s - %d\n", data->version, data->version_num);
    mp_printf(print, "  Host: %s\n", data->host);
    if (data->features & CURL_VERSION_IPV6) {
        mp_printf(print, "    - IP V6 supported\n");
    } else {
        mp_printf(print, "    - IP V6 NOT supported\n");
    }
    if (data->features & CURL_VERSION_SSL) {
        mp_printf(print, "    - SSL supported\n");
    } else {
        mp_printf(print, "    - SSL NOT supported\n");
    }
    if (data->features & CURL_VERSION_LIBZ) {
        mp_printf(print, "    - LIBZ supported\n");
    } else {
        mp_printf(print, "    - LIBZ NOT supported\n");
    }
    if (data->features & CURL_VERSION_DEBUG) {
        mp_printf(print, "    - DEBUG supported\n");
    } else {
        mp_printf(print, "    - DEBUG NOT supported\n");
    }
    mp_printf(print, "  Protocols:\n");
    int i=0;
    while(data->protocols[i] != NULL) {
        mp_printf(print, "    - %s\n", data->protocols[i]);
        i++;
    }
    mp_printf(print, ")\n");
}

//-----------------------------
STATIC mp_obj_t curl_info(void)
{

	print_curl_info(&mp_plat_print);
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(curl_info_obj, curl_info);

//--------------------------------------------------------------------------------------
STATIC mp_obj_t curl_Options(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_print, ARG_verbose, ARG_progress, ARG_timeout, ARG_maxfsize, ARG_hdrlen, ARG_bodylen, ARG_nodecode };
	const mp_arg_t allowed_args[] = {
		{ MP_QSTR_print,                      MP_ARG_BOOL, { .u_bool = true } },
		{ MP_QSTR_verbose,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_progress, MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_timeout,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_maxfsize, MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_hdrlen,   MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_bodylen,  MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
		{ MP_QSTR_nodecode, MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = -1 } },
	};

	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_verbose].u_int >= 0) curl_verbose = args[ARG_verbose].u_int & 1;
    if (args[ARG_progress].u_int >= 0) curl_progress = args[ARG_progress].u_int;
    if (args[ARG_timeout].u_int >= 10) curl_timeout = args[ARG_timeout].u_int;
    if (args[ARG_maxfsize].u_int > 1000) curl_maxbytes = args[ARG_maxfsize].u_int;
    if (args[ARG_hdrlen].u_int > MIN_HDR_BUF_LEN) hdr_maxlen = args[ARG_hdrlen].u_int;
    if (args[ARG_bodylen].u_int > MIN_BODY_BUF_LEN) body_maxlen = args[ARG_bodylen].u_int;
    if (args[ARG_nodecode].u_int >= 0) curl_nodecode = args[ARG_nodecode].u_int & 1;

    if (args[ARG_print].u_bool) {
        mp_printf(&mp_plat_print, "Curl options(\n  Verbose: %s, Progress: %d, Timeout: %d, Max fsize: %d, Header len: %d, Body len: %d, Decode content: %s\n)\n",
        		((curl_verbose) ? "True" : "False"), curl_progress, curl_timeout, curl_maxbytes, hdr_maxlen, body_maxlen, ((curl_nodecode) ? "False" : "True"));

    }
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_Options_obj, 0, curl_Options);

//----------------------------------------------------------------------------------
STATIC mp_obj_t curl_GET(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	network_checkConnection();
    enum { ARG_url, ARG_file };
	const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,      MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_file,                       MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int res;
    int hdr_len = hdr_maxlen;
    int body_len = body_maxlen;
    vstr_t header;
    vstr_t body;
    char *url = NULL;
    char *fname = NULL;
	char fullname[128] = {'\0'};

	url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

    if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
    	// GET to file
		fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
		if (strcmp(fname, "simulate") != 0) {
			res = physicalPath(fname, fullname);
			if ((res != 0) || (strlen(fullname) == 0)) {
				nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
			}
		    fname = fullname;
		}
		body_len = MIN_BODY_BUF_LEN;
    }

    vstr_init_len(&header, hdr_len);
    vstr_init_len(&body, body_len);
    header.buf[0] = '\0';
    body.buf[0] = '\0';

   	MP_THREAD_GIL_EXIT();
   	res = Curl_GET(url, fname, header.buf, body.buf, header.len, body.len);
   	MP_THREAD_GIL_ENTER();

   	mp_obj_t tuple[3];
	tuple[0] = mp_obj_new_int(res);
	header.len = strlen(header.buf);
	body.len = strlen(body.buf);
	tuple[1] = mp_obj_new_str_from_vstr(&mp_type_str, &header);
	tuple[2] = mp_obj_new_str_from_vstr(&mp_type_str, &body);

   	return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_GET_obj, 1, curl_GET);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t curl_GET_MAIL(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    network_checkConnection();
    enum { ARG_opts, ARG_user, ARG_pass, ARG_server, ARG_port, ARG_file, ARG_req };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_opts,     MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_user,     MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_password, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_server,   MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_port,     MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = GMAIL_IMAP_PORT } },
        { MP_QSTR_file,     MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_req,      MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *cust_req = NULL;
    char *opts = (char *)mp_obj_str_get_str(args[ARG_opts].u_obj);
    char *user = (char *)mp_obj_str_get_str(args[ARG_user].u_obj);
    char *pass = (char *)mp_obj_str_get_str(args[ARG_pass].u_obj);
    char mail_server[128] = {'\0'};
    int res;
    int hdr_len = hdr_maxlen;
    int body_len = body_maxlen;
    vstr_t header;
    vstr_t body;
    char *fname = NULL;
    char fullname[128] = {'\0'};

    uint32_t mail_port = args[ARG_port].u_int;
    if (MP_OBJ_IS_STR(args[ARG_req].u_obj)) {
        (char *)mp_obj_str_get_str(args[ARG_req].u_obj);
    }

    if (MP_OBJ_IS_STR(args[ARG_server].u_obj)) sprintf(mail_server, "%s", (char *)mp_obj_str_get_str(args[ARG_server].u_obj));
    else sprintf(mail_server, GMAIL_IMAP);

    if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
        // GET to file
        fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
        if (strcmp(fname, "simulate") != 0) {
            res = physicalPath(fname, fullname);
            if ((res != 0) || (strlen(fullname) == 0)) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
            }
            fname = fullname;
        }
        body_len = MIN_BODY_BUF_LEN;
    }

    vstr_init_len(&header, hdr_len);
    vstr_init_len(&body, body_len);
    header.buf[0] = '\0';
    body.buf[0] = '\0';

    MP_THREAD_GIL_EXIT();
    res = Curl_IMAP_GET(opts, fname, header.buf, body.buf, header.len, body.len, mail_server, mail_port, user, pass, cust_req);
    MP_THREAD_GIL_ENTER();

    mp_obj_t tuple[3];
    tuple[0] = mp_obj_new_int(res);
    header.len = strlen(header.buf);
    body.len = strlen(body.buf);
    tuple[1] = mp_obj_new_str_from_vstr(&mp_type_str, &header);
    tuple[2] = mp_obj_new_str_from_vstr(&mp_type_str, &body);

    return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_GET_MAIL_obj, 1, curl_GET_MAIL);

//-----------------------------------------------------------------------------------
STATIC mp_obj_t curl_POST(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	network_checkConnection();
    enum { ARG_url, ARG_params };
	const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,      MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_params,   MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int res;
    int hdr_len = hdr_maxlen;
    int body_len = body_maxlen;
    vstr_t header;
    vstr_t body;
    char *url = NULL;
	char fullname[128] = {'\0'};

	url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

	if (formpost) curl_formfree(formpost);
	formpost=NULL;
	lastptr=NULL;
    int nparam = 0;

	// get POST parameters
    if (MP_OBJ_IS_TYPE(args[ARG_params].u_obj, &mp_type_dict)) {
        const char *key;
        const char *value;
        mp_obj_dict_t *dict = MP_OBJ_TO_PTR(args[ARG_params].u_obj);
        size_t max = dict->map.alloc;
        mp_map_t *map = &dict->map;
        mp_map_elem_t *next;
        size_t cur = 0;
        while (1) {
            next = NULL;
            for (size_t i = cur; i < max; i++) {
                if (MP_MAP_SLOT_IS_FILLED(map, i)) {
                    cur = i + 1;
                    next = &(map->table[i]);
                    break;
                }
            }
            if (next == NULL) break;

            key = mp_obj_str_get_str(next->key);
            if (MP_OBJ_IS_STR(next->value)) {
                value = mp_obj_str_get_str(next->value);
                uint8_t  fadded = 0;
                if (strlen(value) < 128) {
                    res = physicalPath(value, fullname);
                    if ((res == 0) && (strlen(fullname) > 0)) {
                        if (check_file(fullname)) {
                            curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, key, CURLFORM_FILE, fullname, CURLFORM_END);
                            nparam++;
                            fadded = 1;
                        }
                    }
                }
                if (fadded == 0) {
                    curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, key, CURLFORM_COPYCONTENTS, value, CURLFORM_END);
                    nparam++;
                }
            }
            else if (MP_OBJ_IS_INT(next->value)) {
                int ival = mp_obj_get_int(next->value);
                char sval[64];
                sprintf(sval,"%d", ival);
                curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, key, CURLFORM_COPYCONTENTS, sval, CURLFORM_END);
                nparam++;
            }
            else if (MP_OBJ_IS_TYPE(next->value, &mp_type_float)) {
                double fval = mp_obj_get_float(next->value);
                char sval[64];
                sprintf(sval,"%f", fval);
                curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, key, CURLFORM_COPYCONTENTS, sval, CURLFORM_END);
                nparam++;
            }
        }
		if (nparam == 0) {
			nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Expected at least one POST parameter"));
		}
    }
    else {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Expected Dict type argument"));
    }

    vstr_init_len(&header, hdr_len);
    vstr_init_len(&body, body_len);
    header.buf[0] = '\0';
    body.buf[0] = '\0';

   	MP_THREAD_GIL_EXIT();
    res = Curl_POST(url, header.buf, body.buf, header.len, body.len);
   	MP_THREAD_GIL_ENTER();

   	mp_obj_t tuple[3];
	header.len = strlen(header.buf);
	body.len = strlen(body.buf);
	tuple[0] = mp_obj_new_int(res);
	tuple[1] = mp_obj_new_str_from_vstr(&mp_type_str, &header);
	tuple[2] = mp_obj_new_str_from_vstr(&mp_type_str, &body);

   	return mp_obj_new_tuple(3, tuple);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_POST_obj, 1, curl_POST);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t curl_sendmail(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	network_checkConnection();
    enum { ARG_user, ARG_pass, ARG_to, ARG_subject, ARG_msg, ARG_cc, ARG_bcc, ARG_attach, ARG_server, ARG_port, ARG_prot };
	const mp_arg_t allowed_args[] = {
        { MP_QSTR_user,     MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_password, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_to,       MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_subject,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_msg,      MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_cc,       MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_bcc,      MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_attach,   MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_server,   MP_ARG_KW_ONLY  | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_port,     MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = GMAIL_PORT } },
		{ MP_QSTR_protocol, MP_ARG_KW_ONLY  | MP_ARG_INT, { .u_int = CURLMAIL_PROTOCOL_SMTPS } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

	char *user = (char *)mp_obj_str_get_str(args[ARG_user].u_obj);
	char *pass = (char *)mp_obj_str_get_str(args[ARG_pass].u_obj);
	char *subj = (char *)mp_obj_str_get_str(args[ARG_subject].u_obj);
	char *msg = (char *)mp_obj_str_get_str(args[ARG_msg].u_obj);
	char *to = NULL;
	char mail_server[128] = {'\0'};

	uint32_t mail_port = args[ARG_port].u_int;
	uint8_t mail_protocol = args[ARG_prot].u_int;
	if ((mail_protocol != CURLMAIL_PROTOCOL_SMTP) && (mail_protocol != CURLMAIL_PROTOCOL_SMTPS)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Unsupported SMTP protocol"));
	}
    if (MP_OBJ_IS_STR(args[ARG_server].u_obj)) {
    	sprintf(mail_server, "%s", (char *)mp_obj_str_get_str(args[ARG_server].u_obj));
    }
    else sprintf(mail_server, GMAIL_SMTP);

    // Create curlmail object
	curl_mail mailobj =  curlmail_create(user, subj);
	if (mailobj == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error creating mail object"));
	}

	// Add recipients
    if (MP_OBJ_IS_STR(args[ARG_to].u_obj)) {
    	to = (char *)mp_obj_str_get_str(args[ARG_to].u_obj);
    	curlmail_add_to(mailobj, to);
    }
    else if (MP_OBJ_IS_TYPE(args[ARG_to].u_obj, &mp_type_tuple)) {
        mp_obj_t *items;
        uint len;
        mp_obj_tuple_get(args[ARG_to].u_obj, &len, &items);
        if (len == 0) {
        	curlmail_destroy(mailobj);
        	curl_global_cleanup();
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Recipient(s) expected"));
        }
        for (int i = 0; i < len; i++) {
        	to = (char *)mp_obj_str_get_str(items[i]);
        	curlmail_add_to(mailobj, to);
        }
    }
    else {
    	curlmail_destroy(mailobj);
    	curl_global_cleanup();
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Recipient(s) expected"));
    }

	// Add CC recipients
    if (MP_OBJ_IS_STR(args[ARG_cc].u_obj)) {
    	to = (char *)mp_obj_str_get_str(args[ARG_cc].u_obj);
    	curlmail_add_cc(mailobj, to);
    }
    else if (MP_OBJ_IS_TYPE(args[ARG_cc].u_obj, &mp_type_tuple)) {
        mp_obj_t *items;
        uint len;
        mp_obj_tuple_get(args[ARG_cc].u_obj, &len, &items);
        for (int i = 0; i < len; i++) {
        	to = (char *)mp_obj_str_get_str(items[i]);
        	curlmail_add_cc(mailobj, to);
        }
    }

	// Add BCC recipients
    if (MP_OBJ_IS_STR(args[ARG_bcc].u_obj)) {
    	to = (char *)mp_obj_str_get_str(args[ARG_bcc].u_obj);
    	curlmail_add_bcc(mailobj, to);
    }
    else if (MP_OBJ_IS_TYPE(args[ARG_bcc].u_obj, &mp_type_tuple)) {
        mp_obj_t *items;
        uint len;
        mp_obj_tuple_get(args[ARG_bcc].u_obj, &len, &items);
        for (int i = 0; i < len; i++) {
        	to = (char *)mp_obj_str_get_str(items[i]);
        	curlmail_add_bcc(mailobj, to);
        }
    }

    // Add attachments
    if (MP_OBJ_IS_STR(args[ARG_attach].u_obj)) {
		char *fname = (char *)mp_obj_str_get_str(args[ARG_attach].u_obj);
		char fullname[128] = {'\0'};
		int res = physicalPath(fname, fullname);
	    if ((res == 0) && (strlen(fullname) > 0)) {
	    	int exists = check_file(fullname);
	    	if (exists) curlmail_add_attachment_file(mailobj, fullname, NULL);
	    }
    }
    else if (MP_OBJ_IS_TYPE(args[ARG_attach].u_obj, &mp_type_tuple)) {
        mp_obj_t *items;
        uint len;
        mp_obj_tuple_get(args[ARG_attach].u_obj, &len, &items);
        if (len > CURLMAIL_MAX_ATTACHMENTS) {
        	curlmail_destroy(mailobj);
        	curl_global_cleanup();
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Maximum number of attachments exceeded"));
        }
		char *fname = NULL;
		char fullname[128] = {'\0'};
        for (int i = 0; i < len; i++) {
        	fname = (char *)mp_obj_str_get_str(items[i]);
    		int res = physicalPath(fname, fullname);
    	    if ((res == 0) && (strlen(fullname) > 0)) {
    	    	int exists = check_file(fullname);
    	    	if (exists) curlmail_add_attachment_file(mailobj, fullname, NULL);
    	    }
        }
    }

	curlmail_set_body(mailobj, msg);

	// set headers
	curlmail_add_header(mailobj, "Importance: Low");
	curlmail_add_header(mailobj, "X-Priority: 5");
	curlmail_add_header(mailobj, "X-MSMail-Priority: Low");

	const char* errmsg = NULL;
	errmsg = curlmail_protocol_send(mailobj, mail_server, mail_port, mail_protocol, user, pass);

	// Cleanup
	curlmail_destroy(mailobj);
	curl_global_cleanup();

	if (errmsg) {
		if (curl_verbose) mp_printf(&mp_plat_print, "ERROR: %s\n", errmsg);
		return mp_const_false;
	}
	return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_sendmail_obj, 1, curl_sendmail);


#ifdef CONFIG_MICROPY_USE_CURLFTP

//-------------------------------------------------------------------------------------------------------
STATIC mp_obj_t curl_FTP_helper(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args, uint8_t type)
{
	network_checkConnection();
    enum { ARG_url, ARG_user, ARG_pass, ARG_file };
	const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,  	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_user, 	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_password,	MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_file,                       MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int res;
    int hdr_len = hdr_maxlen;
    int body_len = body_maxlen;
    vstr_t header;
    vstr_t body;
    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);;
    const char *user = mp_obj_str_get_str(args[ARG_user].u_obj);;
    const char *pass = mp_obj_str_get_str(args[ARG_pass].u_obj);;
    char userpass[64];
    char *fname = NULL;
	char fullname[128] = {'\0'};

	if (strstr(url, "ftp://") != url) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "URL must start with 'ftp://'"));
	}

	sprintf(userpass, "%s:%s", user, pass);

	if ((type == 0) || (type == 1)) {
		if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
			// GET to file
			fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
			if (strcmp(fname, "simulate") != 0) {
				res = physicalPath(fname, fullname);
				if ((res != 0) || (strlen(fullname) == 0)) {
					nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
				}
				fname = fullname;
			}
			body_len = MIN_BODY_BUF_LEN;
		}
		else if (type == 1) {
			nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Expected file name for PUT command"));
		}
	}

    vstr_init_len(&header, hdr_len);
    vstr_init_len(&body, body_len);
    header.buf[0] = '\0';
    body.buf[0] = '\0';

   	MP_THREAD_GIL_EXIT();
   	res = Curl_FTP(type&1, (char *)url, userpass, fname, header.buf, body.buf, header.len, body.len);
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
STATIC mp_obj_t curl_FTP_GET(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_FTP_helper(n_args, pos_args, kw_args, 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_FTP_GET_obj, 3, curl_FTP_GET);

//--------------------------------------------------------------------------------------
STATIC mp_obj_t curl_FTP_PUT(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_FTP_helper(n_args, pos_args, kw_args, 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_FTP_PUT_obj, 3, curl_FTP_PUT);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t curl_FTP_LIST(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	return curl_FTP_helper(n_args, pos_args, kw_args, 2);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(curl_FTP_LIST_obj, 3, curl_FTP_LIST);

#endif


//============================================================
STATIC const mp_rom_map_elem_t curl_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_curl) },

    { MP_ROM_QSTR(MP_QSTR_info),		MP_ROM_PTR(&curl_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_options),		MP_ROM_PTR(&curl_Options_obj) },
    { MP_ROM_QSTR(MP_QSTR_get),			MP_ROM_PTR(&curl_GET_obj) },
    { MP_ROM_QSTR(MP_QSTR_post),		MP_ROM_PTR(&curl_POST_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendmail),	MP_ROM_PTR(&curl_sendmail_obj) },
    { MP_ROM_QSTR(MP_QSTR_getmail),     MP_ROM_PTR(&curl_GET_MAIL_obj) },

	#ifdef CONFIG_MICROPY_USE_CURLFTP
    { MP_ROM_QSTR(MP_QSTR_ftp_get),		MP_ROM_PTR(&curl_FTP_GET_obj) },
    { MP_ROM_QSTR(MP_QSTR_ftp_put),		MP_ROM_PTR(&curl_FTP_PUT_obj) },
    { MP_ROM_QSTR(MP_QSTR_ftp_list),	MP_ROM_PTR(&curl_FTP_LIST_obj) },
	#endif

	// Constants
	{ MP_ROM_QSTR(MP_QSTR_SMTP),		MP_ROM_INT(CURLMAIL_PROTOCOL_SMTP) },
    { MP_ROM_QSTR(MP_QSTR_SMTPS),		MP_ROM_INT(CURLMAIL_PROTOCOL_SMTPS) },
};
STATIC MP_DEFINE_CONST_DICT(curl_module_globals, curl_module_globals_table);

//======================================
const mp_obj_module_t mp_module_curl = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&curl_module_globals,
};

#endif

