/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 "Boris Lovosevic" <https://github.com/loboris>
 *
 * Based on the ESP IDF example code which is Public Domain / CC0
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

#ifdef CONFIG_MICROPY_USE_MDNS

#include <string.h>
#include "mdns.h"

#include "py/runtime.h"
#include "py/mphal.h"

#include "modnetwork.h"

#define MDNS_NAME_LEN	32


typedef struct _mdns_obj_t {
    mp_obj_base_t base;
    wlan_if_obj_t *if_obj;
    mdns_server_t * mdns;
    char hostname[MDNS_NAME_LEN+1];
    char instance[MDNS_NAME_LEN+1];
} mdns_obj_t;

const mp_obj_type_t mdns_type;

//-------------------------------------------------------------------------------------
STATIC void mdns_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    mdns_obj_t *self = self_in;

    if (self->mdns == NULL) {
    	mp_printf(print, "mDNS( Not started )\n");
    	return;
    }
    char s_if[8];
    if (self->if_obj->if_id == TCPIP_ADAPTER_IF_STA) sprintf(s_if, "IF_STA");
    else if (self->if_obj->if_id == TCPIP_ADAPTER_IF_AP) sprintf(s_if, "IF_AP");
    else sprintf(s_if, "Unknown");

    mp_printf(print, "mDNS[%s] (Server name: %s, Instance name: %s)\n", s_if, self->hostname, self->instance);
}

//------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    const mp_arg_t mdns_init_allowed_args[] = {
			{ MP_QSTR_if,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(mdns_init_allowed_args), mdns_init_allowed_args, args);

    // Setup the mqtt object
    mdns_obj_t *self = m_new_obj(mdns_obj_t );
    self->mdns = NULL;

    wlan_if_obj_t *if_obj = (wlan_if_obj_t *)args[0].u_obj;

    if (MP_OBJ_IS_TYPE(if_obj, &wlan_if_type)) {
        self->hostname[0] = '\0';
        self->instance[0] = '\0';
        self->if_obj = if_obj;
        self->base.type = &mdns_type;

        return MP_OBJ_FROM_PTR(self);
    }
	mp_raise_msg(&mp_type_OSError, "WLAN STA or AP object expected");
    return mp_const_none;
}

//---------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_start(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_name, ARG_instance };

    const mp_arg_t mdns_allowed_args[] = {
			{ MP_QSTR_name,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_instance,	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mdns_allowed_args), mdns_allowed_args, args);

    mdns_obj_t *self = pos_args[0];
    esp_err_t err;

    snprintf(self->hostname, MDNS_NAME_LEN, mp_obj_str_get_str(args[ARG_name].u_obj));
    snprintf(self->instance, MDNS_NAME_LEN, mp_obj_str_get_str(args[ARG_instance].u_obj));
    self->base.type = &mdns_type;

    if (!self->mdns) {
        err = mdns_init(self->if_obj->if_id, &self->mdns);
        if (err) mp_raise_msg(&mp_type_OSError, "Error initializing mDNS server.");
    }
    else mp_raise_msg(&mp_type_OSError, "mDNS server already started.");

    err = mdns_set_hostname(self->mdns, self->hostname);
    if (err) {
    	mdns_free(self->mdns);
    	self->mdns = NULL;
    	mp_raise_ValueError("Error setting server name.");
    }
    err = mdns_set_instance(self->mdns, self->instance);
    if (err) {
    	mdns_free(self->mdns);
    	self->mdns = NULL;
    	mp_raise_ValueError("Error setting server instance.");
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_start_obj, 1, mdns_start);

//-----------------------------------------
STATIC mp_obj_t mdns_stop(mp_obj_t self_in)
{
    mdns_obj_t *self = self_in;

	if (self->mdns) mdns_free(self->mdns);
	self->mdns = NULL;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mdns_stop_obj, mdns_stop);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_add_service(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_service, ARG_proto, ARG_port, ARG_instance, ARG_txdata };

    const mp_arg_t mdns_allowed_args[] = {
			{ MP_QSTR_service,	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_protocol,	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_port,		MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_instance,	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_txdata,	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mdns_allowed_args), mdns_allowed_args, args);

    mdns_obj_t *self = pos_args[0];

    if (!self->mdns) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *instance = NULL;
    uint8_t ntxdata = 0;
    const char *svctxdata[8] = {NULL};

    const char *service = mp_obj_str_get_str(args[ARG_service].u_obj);
    const char *protocol = mp_obj_str_get_str(args[ARG_proto].u_obj);
    int port = args[ARG_port].u_int;
    if (MP_OBJ_IS_STR(args[ARG_instance].u_obj)) {
    	instance = mp_obj_str_get_str(args[ARG_instance].u_obj);
    }
    if (MP_OBJ_IS_STR(args[ARG_txdata].u_obj)) {
    	svctxdata[0] = mp_obj_str_get_str(args[ARG_txdata].u_obj);
    	ntxdata = 1;
    }
    else if (MP_OBJ_IS_TYPE(args[ARG_txdata].u_obj, &mp_type_tuple)) {
        mp_obj_t *items;
        uint len;
        mp_obj_tuple_get(args[ARG_txdata].u_obj, &len, &items);
        if (len > 8) len = 8;
        for (int i = 0; i < len; i++) {
        	svctxdata[i] = mp_obj_str_get_str(items[i]);
        	ntxdata++;
        }
    }

    //add the services
    if (mdns_service_add(self->mdns, service, protocol, port) != ESP_OK) return mp_const_false;

    if (instance) {
		//NOTE: services must be added before their properties can be set
		if (mdns_service_instance_set(self->mdns, service, protocol, instance) != ESP_OK) return mp_const_false;
    }

    if (ntxdata) {
		//set text data for service (will free and replace current data)
		if (mdns_service_txt_set(self->mdns, service, protocol, ntxdata, svctxdata) != ESP_OK) return mp_const_false;
    }

    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_add_service_obj, 1, mdns_add_service);

//------------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_remove_service(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_service, ARG_proto };

    const mp_arg_t mdns_allowed_args[] = {
			{ MP_QSTR_service,	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_protocol,	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mdns_allowed_args), mdns_allowed_args, args);

    mdns_obj_t *self = pos_args[0];

    if (!self->mdns) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *service = mp_obj_str_get_str(args[ARG_service].u_obj);
    const char *protocol = mp_obj_str_get_str(args[ARG_proto].u_obj);

    //remove the services
    if (mdns_service_remove(self->mdns, service, protocol) != ESP_OK) return mp_const_false;

    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_remove_service_obj, 1, mdns_remove_service);

//--------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_host_query(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t mdns_allowed_args[] = {
			{ MP_QSTR_hostname,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mdns_allowed_args), mdns_allowed_args, args);

    mdns_obj_t *self = pos_args[0];

    if (!self->mdns) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *hostname = mp_obj_str_get_str(args[0].u_obj);

    mp_obj_t list = mp_obj_new_list(0, NULL);
    uint32_t res;
    char tmps[64];

	mp_obj_tuple_t *t = mp_obj_new_tuple(2, NULL);
	// Host Lookup
	res = mdns_query(self->mdns, hostname, 0, 2000);
	if (res) {
		size_t i;
		for(i=0; i<res; i++) {
			const mdns_result_t * r = mdns_result_get(self->mdns, i);
			if (r) {
				sprintf(tmps, IPSTR,  IP2STR(&r->addr));
				t->items[0] = mp_obj_new_str(tmps, strlen(tmps), false);

				sprintf(tmps, IPV6STR, IPV62STR(r->addrv6));
				t->items[1] = mp_obj_new_str(tmps, strlen(tmps), false);

				mp_obj_list_append(list, MP_OBJ_FROM_PTR(t));
			}
		}
		mdns_result_free(self->mdns);
	}

    return list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_host_query_obj, 2, mdns_host_query);


//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_service_query(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t mdns_allowed_args[] = {
			{ MP_QSTR_service,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_protocol,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mdns_allowed_args), mdns_allowed_args, args);

    mdns_obj_t *self = pos_args[0];

    if (!self->mdns) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *service = mp_obj_str_get_str(args[0].u_obj);
    const char *proto = mp_obj_str_get_str(args[1].u_obj);

    mp_obj_t list = mp_obj_new_list(0, NULL);
    uint32_t res;
    char tmps[128];

	mp_obj_tuple_t *t = mp_obj_new_tuple(6, NULL);
	// Service Lookup
	res = mdns_query(self->mdns, service, proto, 2000);
	if (res) {
		size_t i;
		for(i=0; i<res; i++) {
			const mdns_result_t * r = mdns_result_get(self->mdns, i);
			if (r) {
				sprintf(tmps, "%s",  (r->host) ? r->host : "");
				t->items[0] = mp_obj_new_str(tmps, strlen(tmps), false);

				sprintf(tmps, "%s",  (r->instance) ? r->instance : "");
				t->items[1] = mp_obj_new_str(tmps, strlen(tmps), false);

				sprintf(tmps, IPSTR,  IP2STR(&r->addr));
				t->items[2] = mp_obj_new_str(tmps, strlen(tmps), false);

				sprintf(tmps, IPV6STR, IPV62STR(r->addrv6));
				t->items[3] = mp_obj_new_str(tmps, strlen(tmps), false);

				t->items[4] = mp_obj_new_int(r->port);

				sprintf(tmps, "%s",  (r->txt) ? r->txt : "");
				t->items[5] = mp_obj_new_str(tmps, strlen(tmps), false);

				mp_obj_list_append(list, MP_OBJ_FROM_PTR(t));
			}
		}
		mdns_result_free(self->mdns);
	}

    return list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_service_query_obj, 3, mdns_service_query);


//=========================================================
STATIC const mp_rom_map_elem_t mdns_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__),			(mp_obj_t)&mdns_stop_obj },
    { MP_ROM_QSTR(MP_QSTR_start),			(mp_obj_t)&mdns_start_obj },
    { MP_ROM_QSTR(MP_QSTR_stop),			(mp_obj_t)&mdns_stop_obj },
    { MP_ROM_QSTR(MP_QSTR_queryHost),		(mp_obj_t)&mdns_host_query_obj },
    { MP_ROM_QSTR(MP_QSTR_queryService),	(mp_obj_t)&mdns_service_query_obj },
    { MP_ROM_QSTR(MP_QSTR_addService),		(mp_obj_t)&mdns_add_service_obj },
    { MP_ROM_QSTR(MP_QSTR_removeService),	(mp_obj_t)&mdns_remove_service_obj },
};
STATIC MP_DEFINE_CONST_DICT(mdns_locals_dict, mdns_locals_dict_table);

//===============================
const mp_obj_type_t mdns_type = {
    { &mp_type_type },
    .name = MP_QSTR_mDNS,
    .print = mdns_print,
    .make_new = mdns_make_new,
    .locals_dict = (mp_obj_dict_t*)&mdns_locals_dict,
};

#endif
