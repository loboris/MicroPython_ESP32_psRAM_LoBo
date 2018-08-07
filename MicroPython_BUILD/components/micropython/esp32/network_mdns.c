/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
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
#include "netdb.h"


#define MDNS_NAME_LEN	32


typedef struct _mdns_obj_t {
    mp_obj_base_t base;
    uint8_t is_started;
    char hostname[MDNS_NAME_LEN+1];
    char instance[MDNS_NAME_LEN+1];
} mdns_obj_t;

static const char * if_str[] = {"STA", "AP", "ETH", "MAX"};
static const char * ip_protocol_str[] = {"V4", "V6", "MAX"};

const mp_obj_type_t mdns_type;

mdns_obj_t mdns_obj = {0};

//-------------------------------------------------------------------------------------
STATIC void mdns_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    mdns_obj_t *self = self_in;

    if (!self->is_started) {
    	mp_printf(print, "mDNS( Not started )\n");
    	return;
    }

    mp_printf(print, "mDNS(Server name: %s, Instance name: %s)\n", self->hostname, self->instance);
}

//------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    // Get the mdns object
    mdns_obj_t *self = &mdns_obj;
    self->base.type = &mdns_type;

    return MP_OBJ_FROM_PTR(self);
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

    if (!self->is_started) {
    	// If not initialized, host name and instance are mandatory
        snprintf(self->hostname, MDNS_NAME_LEN, mp_obj_str_get_str(args[ARG_name].u_obj));
        snprintf(self->instance, MDNS_NAME_LEN, mp_obj_str_get_str(args[ARG_instance].u_obj));
        err = mdns_init();
        if (err) mp_raise_msg(&mp_type_OSError, "Error initializing mDNS server.");
		//set mDNS hostname (required if we want to advertise services)
		err = mdns_hostname_set(self->hostname);
		if (err) {
			mdns_free();
			mp_raise_ValueError("Error setting server name.");
		}
		err = mdns_instance_name_set(self->instance);
		if (err) {
			mdns_free();
			mp_raise_ValueError("Error setting server instance.");
		}
        self->is_started = 1;
        return mp_const_none;
    }

    if (args[ARG_name].u_obj != mp_const_none) {
    	const char *hostname = mp_obj_str_get_str(args[ARG_name].u_obj);
    	if (strcmp(self->hostname, hostname) != 0) {
			err = mdns_hostname_set(self->hostname);
			if (err) {
				mdns_free();
				self->is_started = 0;
				mp_raise_ValueError("Error setting server name.");
			}
    	}
    }
    if (args[ARG_instance].u_obj != mp_const_none) {
    	const char *instance = mp_obj_str_get_str(args[ARG_instance].u_obj);
    	if (strcmp(self->instance, instance) != 0) {
			err = mdns_instance_name_set(self->instance);
			if (err) {
				mdns_free();
				self->is_started = 0;
				mp_raise_ValueError("Error setting server instance.");
			}
    	}
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_start_obj, 1, mdns_start);

//-----------------------------------------
STATIC mp_obj_t mdns_stop(mp_obj_t self_in)
{
    mdns_obj_t *self = self_in;

	if (self->is_started) mdns_free();
    self->is_started = 0;
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

    if (!self->is_started) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *instance = NULL;
    uint8_t ntxdata = 0;
    mdns_txt_item_t svctxdata[8] = {0};

    const char *service = mp_obj_str_get_str(args[ARG_service].u_obj);
    if (service[0] != '_') {
		mp_raise_ValueError("Service name must start with '_'");
    }
    const char *protocol = mp_obj_str_get_str(args[ARG_proto].u_obj);
    if ((strcmp(protocol, "_tcp") != 0) && (strcmp(protocol, "_udp") != 0)) {
		mp_raise_ValueError("Protocol must be '_tcp' or '_udp'");
    }
    int port = args[ARG_port].u_int;
    if ((port < 1) || (port > 0xFFFF)) {
		mp_raise_ValueError("Wrong port value");
    }
    if (MP_OBJ_IS_STR(args[ARG_instance].u_obj)) {
    	instance = mp_obj_str_get_str(args[ARG_instance].u_obj);
    }

    if (MP_OBJ_IS_TYPE(args[ARG_txdata].u_obj, &mp_type_dict)) {
        mp_obj_dict_t *dict = MP_OBJ_TO_PTR(args[ARG_txdata].u_obj);
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

            svctxdata[ntxdata].key = (char *)mp_obj_str_get_str(next->key);
            svctxdata[ntxdata].value = (char *)mp_obj_str_get_str(next->value);
            ntxdata++;
            if (ntxdata > 7) break;
        }
    }

    //add the services
    if (mdns_service_add(instance, service, protocol, port, svctxdata, ntxdata) != ESP_OK) return mp_const_false;

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

    if (!self->is_started) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *service = mp_obj_str_get_str(args[ARG_service].u_obj);
    const char *protocol = mp_obj_str_get_str(args[ARG_proto].u_obj);

    //remove the services
    if (mdns_service_remove(service, protocol) != ESP_OK) return mp_const_false;

    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_remove_service_obj, 1, mdns_remove_service);

//--------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_host_query(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t mdns_allowed_args[] = {
			{ MP_QSTR_hostname,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_timeout,   	MP_ARG_INT,  {.u_int = 2000} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mdns_allowed_args), mdns_allowed_args, args);

    mdns_obj_t *self = pos_args[0];

    if (!self->is_started) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *hostname = mp_obj_str_get_str(args[0].u_obj);
    int tmo = args[1].u_int;
    if ((tmo < 100) || (tmo > 10000)) tmo = 2000;
    struct ip4_addr addr;
    addr.addr = 0;

    esp_err_t res;
    char tmps[64] = {0};

	// Host Lookup
	res = mdns_query_a(hostname, 2000, &addr);

	if (res == ESP_OK) sprintf(tmps, IPSTR,  IP2STR(&addr));
	else if (res == ESP_ERR_NOT_FOUND) sprintf(tmps, "Host was not found!");
	else sprintf(tmps, "Query Failed");

    return mp_obj_new_str(tmps, strlen(tmps));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mdns_host_query_obj, 2, mdns_host_query);

//-----------------------------------------------------------------------------------------------
STATIC mp_obj_t mdns_service_query(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t mdns_allowed_args[] = {
			{ MP_QSTR_service,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_protocol,   	MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_timeout,   	MP_ARG_INT,  {.u_int = 2000} },
			{ MP_QSTR_maxres,   	MP_ARG_INT,  {.u_int = 8} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mdns_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mdns_allowed_args), mdns_allowed_args, args);

    mdns_obj_t *self = pos_args[0];

    if (!self->is_started) mp_raise_msg(&mp_type_OSError, "mDNS server not started.");

    const char *service = mp_obj_str_get_str(args[0].u_obj);
    const char *proto = mp_obj_str_get_str(args[1].u_obj);
    if (service[0] != '_') {
		mp_raise_ValueError("Service name must start with '_'");
    }
    if ((strcmp(proto, "_tcp") != 0) && (strcmp(proto, "_udp") != 0)) {
		mp_raise_ValueError("Protocol must be '_tcp' or '_udp'");
    }

    int tmo = args[2].u_int;
    if ((tmo < 100) || (tmo > 10000)) tmo = 2000;

    int maxres = args[3].u_int;
    if ((maxres < 1) || (maxres > 30)) maxres = 10;

    mp_obj_t list = mp_obj_new_list(0, NULL);
    mdns_result_t * results = NULL;
    esp_err_t err = mdns_query_ptr(service, proto, tmo, maxres,  &results);

	// Service Lookup
	if (err == ESP_OK) {
		if (results) {
		    mp_obj_tuple_t *t = mp_obj_new_tuple(7, NULL);
		    char tmps[128];
		    mdns_result_t * r = results;
		    mdns_ip_addr_t * a = NULL;

		    while (r) {
		    	// Interface type
				sprintf(tmps, "%s",  if_str[r->tcpip_if]);
				t->items[0] = mp_obj_new_str(tmps, strlen(tmps));

				// Protocol, V4 or V6
				sprintf(tmps, "%s",  ip_protocol_str[r->ip_protocol]);
				t->items[1] = mp_obj_new_str(tmps, strlen(tmps));

				// Instance name
				if (r->instance_name) sprintf(tmps, "%s", r->instance_name);
				else sprintf(tmps, "?");
				t->items[2] = mp_obj_new_str(tmps, strlen(tmps));

				// Host name & port
				if(r->hostname) {
					sprintf(tmps, "%s.local", r->hostname);
					t->items[3] = mp_obj_new_str(tmps, strlen(tmps));
					t->items[4] = mp_obj_new_int(r->port);
				}
				else {
					t->items[3] = mp_const_none;
					t->items[4] = mp_const_none;
				}

				// IP addresses
		        a = r->addr;
		        if (a) {
		            mp_obj_t addrlist = mp_obj_new_list(0, NULL);
					while (a) {
						if (a->addr.type == MDNS_IP_PROTOCOL_V6) {
							sprintf(tmps, IPV6STR, IPV62STR(a->addr.u_addr.ip6));
						}
						else {
							sprintf(tmps, IPSTR, IP2STR(&(a->addr.u_addr.ip4)));
						}
						mp_obj_list_append(addrlist, mp_obj_new_str(tmps, strlen(tmps)));
			            a = a->next;
					}
					t->items[5] = addrlist;
		        }
		        else t->items[5] = mp_const_none;

		        // Text records
		        if(r->txt_count){
		            mp_obj_dict_t *dct = mp_obj_new_dict(0);
		            for(int i=0; i<r->txt_count; i++){
		            	mp_obj_dict_store(dct,  mp_obj_new_str(r->txt[i].key, strlen(r->txt[i].key)), mp_obj_new_str(r->txt[i].value, strlen(r->txt[i].key)));
		            }
					t->items[6] = dct;
		        }
		        else t->items[6] = mp_const_none;

		        mp_obj_list_append(list, MP_OBJ_FROM_PTR(t));

				r = r->next;
			}
			mdns_query_results_free(results);
		}
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
