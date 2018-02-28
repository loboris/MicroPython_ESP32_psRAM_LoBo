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

/*
 * Mqtt Module using MQTT task.
 * Based on ESP32 MQTT Library by Tuan PM, https://github.com/tuanpmt/espmqtt
 * Adapted for MicroPython by Boris Lovosevic, https://github.com/loboris
 *
 */

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_MQTT

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mqtt.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "mphalport.h"


typedef struct _mqtt_obj_t {
    mp_obj_base_t base;
    mqtt_client *client;
    char name[CONFIG_MQTT_MAX_TASKNAME_LEN];
} mqtt_obj_t;

const mp_obj_type_t mqtt_type;



//--------------------------------------
STATIC int checkClient(mqtt_obj_t *self)
{
	if (self->client == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Mqtt client destroyed"));
    }
	if (self->client->status == MQTT_STATUS_DISCONNECTED) {
        //nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Mqtt client disconnected"));
		return 1;
    }
	if (self->client->status == MQTT_STATUS_STOPPING) {
        //nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Mqtt client stopping"));
		return 2;
    }
	if (self->client->status == MQTT_STATUS_STOPPED) {
        //nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Mqtt client stopped"));
		return 3;
    }
	return 0;
}

//------------------------------------------------
STATIC void connected_cb(void *self, void *params)
{
    mqtt_client *client = (mqtt_client *)self;

    if (client->settings->mpy_connected_cb) {
		mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_SINGLE);
		if (!carg) return;
		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(client->name), (const uint8_t *)client->name, NULL)) return;
		mp_sched_schedule(client->settings->mpy_connected_cb, mp_const_none, carg);
    }
}

//---------------------------------------------------
STATIC void disconnected_cb(void *self, void *params)
{
    mqtt_client *client = (mqtt_client *)self;

    if (client->settings->mpy_disconnected_cb) {
		mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_SINGLE);
		if (!carg) return;
		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(client->name), (const uint8_t *)client->name, NULL)) return;
		mp_sched_schedule(client->settings->mpy_disconnected_cb, mp_const_none, carg);
    }
}

//-------------------------------------------------
STATIC void subscribed_cb(void *self, void *params)
{
    mqtt_client *client = (mqtt_client *)self;
    const char *topic = (const char *)params;

    if (client->settings->mpy_subscribed_cb) {
    	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    	if (carg == NULL) return;
   		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(client->name), (const uint8_t *)client->name, NULL)) return;
   		if (topic) {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen(topic), (const uint8_t *)topic, NULL)) return;
   		}
   		else {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, 1, (const uint8_t *)"?", NULL)) return;
   		}
    	mp_sched_schedule(client->settings->mpy_subscribed_cb, mp_const_none, carg);
    }
}

//---------------------------------------------------
STATIC void unsubscribed_cb(void *self, void *params)
{
    mqtt_client *client = (mqtt_client *)self;
    const char *topic = (const char *)params;

    if (client->settings->mpy_unsubscribed_cb) {
    	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    	if (carg == NULL) return;
   		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(client->name), (const uint8_t *)client->name, NULL)) return;
   		if (topic) {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen(topic), (const uint8_t *)topic, NULL)) return;
   		}
   		else {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, 1, (const uint8_t *)"?", NULL)) return;
   		}
    	mp_sched_schedule(client->settings->mpy_unsubscribed_cb, mp_const_none, carg);
    }
}

//------------------------------------------------
STATIC void published_cb(void *self, void *params)
{
    mqtt_client *client = (mqtt_client *)self;
    const char *type = (const char *)params;

    if (client->settings->mpy_published_cb) {
    	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    	if (carg == NULL) return;
   		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(client->name), (const uint8_t *)client->name, NULL)) return;
   		if (type) {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen(type), (const uint8_t *)type, NULL)) return;
   		}
   		else {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, 1, (const uint8_t *)"?", NULL)) return;
   		}
    	mp_sched_schedule(client->settings->mpy_published_cb, mp_const_none, carg);
    }
}

//-------------------------------------------
STATIC void data_cb(void *self, void *params)
{
    mqtt_client *client = (mqtt_client *)self;
    if (!client->settings->mpy_data_cb) return;

    mqtt_event_data_t *event_data = (mqtt_event_data_t *)params;

	if (event_data->data_offset == 0) {
		// *** First block of data
		if (client->msgbuf != NULL) free(client->msgbuf);
		if (client->topicbuf != NULL) free(client->topicbuf);
		client->msgbuf = NULL;
		client->topicbuf = NULL;
		if (event_data->data_length < event_data->data_total_length) {
			// === more data will follow, allocate the data buffer and copy the first part ===
			client->topicbuf = malloc(event_data->topic_length + 1);
			if (client->topicbuf) {
				memcpy(client->topicbuf, event_data->topic, event_data->topic_length);
				client->topicbuf[event_data->topic_length] = 0;

				int buf_len = event_data->data_total_length + 1;
				client->msgbuf = malloc(buf_len + 1);
				if (client->msgbuf) {
					memcpy(client->msgbuf, event_data->data, event_data->data_length);
					client->msgbuf[event_data->data_length] = 0;
				}
				else {
					free(client->topicbuf);
					client->msgbuf = NULL;
					client->topicbuf = NULL;
				}
			}
		}
		else {
			// === all data received, we can schedule the callback function now ===
			mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
			if (!carg) return;
			if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(client->name), (const uint8_t *)client->name, NULL)) return;
			if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, event_data->topic_length, (const uint8_t *)event_data->topic, NULL)) return;
			if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, event_data->data_length, (const uint8_t *)event_data->data, NULL)) return;
			mp_sched_schedule(client->settings->mpy_data_cb, mp_const_none, carg);
		}
	}
	else {
		if ((client->topicbuf) && (client->msgbuf)) {
			// === more payload data arrived, add to buffer ===
			int new_len = event_data->data_offset + event_data->data_length;
			memcpy(client->msgbuf + event_data->data_offset, event_data->data, event_data->data_length);
			client->msgbuf[new_len] = 0;
			if (new_len >= event_data->data_total_length) {
				// === all data received, we can schedule the callback function now ===
				mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
				if (!carg) goto freebufs;
				if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(client->name), (const uint8_t *)client->name, NULL)) goto freebufs;
				if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen((const char *)client->topicbuf), client->topicbuf, NULL)) goto freebufs;
				if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, event_data->data_total_length, client->msgbuf, NULL)) goto freebufs;
				mp_sched_schedule(client->settings->mpy_data_cb, mp_const_none, carg);
freebufs:
				// Free the buffers
				free(client->msgbuf);
				free(client->topicbuf);
				client->msgbuf = NULL;
				client->topicbuf = NULL;
			}
		}
		else {
			// more payload data arrived, but there is no data buffers allocated (!?)
			if (client->msgbuf != NULL) free(client->msgbuf);
			if (client->topicbuf != NULL) free(client->topicbuf);
			client->msgbuf = NULL;
			client->topicbuf = NULL;
		}
	}
}


//-------------------------------------------------------------------------------------
STATIC void mqtt_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    mqtt_obj_t *self = self_in;

    if (self->client == NULL) {
    	mp_printf(print, "Mqtt[%s]( Destroyed )\n", self->name);
    	return;
    }
    char sstat[16];
    if (self->client->status == MQTT_STATUS_CONNECTED) sprintf(sstat, "Connected");
    else if (self->client->status == MQTT_STATUS_DISCONNECTED) sprintf(sstat, "Disconnected");
    else if (self->client->status == MQTT_STATUS_STOPPING) sprintf(sstat, "Stopping");
    else if (self->client->status == MQTT_STATUS_STOPPED) sprintf(sstat, "Stopped");
    else sprintf(sstat, "Unknown");

    mp_printf(print, "Mqtt[%s](Server: %s:%u, Status: %s\n", self->name, self->client->settings->host, self->client->settings->port, sstat);
    if ((self->client->status != MQTT_STATUS_STOPPING) && (self->client->status != MQTT_STATUS_STOPPED)) {
		mp_printf(print, "     Client ID: %s, Clean session=%s, Keepalive=%d sec, QoS=%d, Retain=%s, Secure=%s\n",
				self->client->settings->client_id, (self->client->settings->clean_session ? "True" : "False"), self->client->settings->keepalive, self->client->settings->lwt_qos,
				(self->client->settings->lwt_retain ? "True" : "False"), (self->client->settings->use_ssl ? "True" : "False"));
    }
	if ((self->client->settings->xMqttTask) && (self->client->settings->xMqttSendingTask)) {
		mp_printf(print, "     Used stack: %u/%u + %u/%u\n",
				self->client->settings->xMqttTask_stacksize - uxTaskGetStackHighWaterMark(self->client->settings->xMqttTask), self->client->settings->xMqttTask_stacksize,
				self->client->settings->xMqttSendingTask_stacksize - uxTaskGetStackHighWaterMark(self->client->settings->xMqttSendingTask), self->client->settings->xMqttSendingTask_stacksize);
	}
	mp_printf(print, "    )\n");
}


//------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mqtt_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	enum { ARG_name, ARG_host, ARG_user, ARG_pass, ARG_port, ARG_reconnect, ARG_clientid, ARG_cleansess, ARG_keepalive, ARG_qos, ARG_retain, ARG_secure,
		   ARG_datacb, ARG_connected, ARG_disconnected, ARG_subscribed, ARG_unsubscribed, ARG_published };

    const mp_arg_t mqtt_init_allowed_args[] = {
			{ MP_QSTR_name,   	    	MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_server,       	MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_user,         	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_password,        	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_port,         	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_autoreconnect,   	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_clientid,     	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_cleansession, 	MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false} },
			{ MP_QSTR_keepalive,    	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 120} },
			{ MP_QSTR_qos,          	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_retain,  		    MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_secure,       	MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false} },
			{ MP_QSTR_data_cb,       	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_connected_cb,  	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_disconnected_cb,	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_subscribed_cb,  	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_unsubscribed_cb, 	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_published_cb,		MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mqtt_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(mqtt_init_allowed_args), mqtt_init_allowed_args, args);

    // Setup the mqtt object
    mqtt_obj_t *self = m_new_obj(mqtt_obj_t );

    // === allocate client memory ===
    self->client = malloc(sizeof(mqtt_client));
    if (self->client == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "Error allocating client memory"));
    }
    memset(self->client, 0, sizeof(mqtt_client));

    self->client->settings = malloc(sizeof(mqtt_settings));
    if (self->client->settings == NULL) {
    	free(self->client);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "Error allocating client memory"));
    }
    memset(self->client->settings, 0, sizeof(mqtt_settings));

    // Populate settings
    self->client->settings->use_ssl = args[ARG_secure].u_bool;

    snprintf(self->name, CONFIG_MQTT_MAX_TASKNAME_LEN, mp_obj_str_get_str(args[ARG_name].u_obj));
    self->client->name = self->name;
    snprintf(self->client->settings->host, CONFIG_MQTT_MAX_HOST_LEN, mp_obj_str_get_str(args[ARG_host].u_obj));

    if (args[ARG_port].u_int > 0) self->client->settings->port = args[ARG_port].u_int;
    else if (args[ARG_secure].u_bool) self->client->settings->port = 8883;
    else self->client->settings->port = 1883;

    if (MP_OBJ_IS_STR(args[ARG_user].u_obj)) {
        snprintf(self->client->settings->username, CONFIG_MQTT_MAX_USERNAME_LEN, mp_obj_str_get_str(args[ARG_user].u_obj));
    }
    if (MP_OBJ_IS_STR(args[ARG_pass].u_obj)) {
    	snprintf(self->client->settings->password, CONFIG_MQTT_MAX_PASSWORD_LEN, mp_obj_str_get_str(args[ARG_pass].u_obj));
    }
    if (MP_OBJ_IS_STR(args[ARG_clientid].u_obj)) {
        snprintf(self->client->settings->client_id, CONFIG_MQTT_MAX_CLIENT_LEN, mp_obj_str_get_str(args[ARG_clientid].u_obj));
    }
    else sprintf(self->client->settings->client_id, "mpy_mqtt_client");

    self->client->settings->auto_reconnect = args[ARG_reconnect].u_int;
    self->client->settings->keepalive = args[ARG_keepalive].u_int;
    self->client->settings->clean_session = args[ARG_cleansess].u_int;
    sprintf(self->client->settings->lwt_topic, "/lwt");
    sprintf(self->client->settings->lwt_msg, "offline");
    self->client->settings->lwt_qos = args[ARG_qos].u_int;
    self->client->settings->lwt_retain = args[ARG_retain].u_int;

    // set callbacks
    if ((MP_OBJ_IS_FUN(args[ARG_datacb].u_obj)) || (MP_OBJ_IS_METH(args[ARG_datacb].u_obj))) {
	    self->client->settings->data_cb = (void*)data_cb;
	    self->client->settings->mpy_data_cb = args[ARG_datacb].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_connected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_connected].u_obj))) {
	    self->client->settings->connected_cb = (void*)connected_cb;
	    self->client->settings->mpy_connected_cb = args[ARG_connected].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_disconnected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_disconnected].u_obj))) {
	    self->client->settings->disconnected_cb = (void*)disconnected_cb;
	    self->client->settings->mpy_disconnected_cb = args[ARG_disconnected].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_subscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_subscribed].u_obj))) {
	    self->client->settings->subscribe_cb = (void*)subscribed_cb;
	    self->client->settings->mpy_subscribed_cb = args[ARG_subscribed].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_unsubscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_unsubscribed].u_obj))) {
	    self->client->settings->unsubscribe_cb = (void*)unsubscribed_cb;
	    self->client->settings->mpy_unsubscribed_cb = args[ARG_unsubscribed].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_published].u_obj)) || (MP_OBJ_IS_METH(args[ARG_published].u_obj))) {
	    self->client->settings->publish_cb = (void*)published_cb;
	    self->client->settings->mpy_published_cb = args[ARG_published].u_obj;
	}

    // Start the mqtt task
    int res = mqtt_start(self->client);
    if (res != 0) {
    	free(self->client->settings);
    	free(self->client);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "Error starting client"));
    }

    self->base.type = &mqtt_type;

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_config(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_clientid, ARG_reconnect, ARG_cleansess, ARG_keepalive, ARG_qos, ARG_retain, ARG_secure,
		   ARG_datacb, ARG_connected, ARG_disconnected, ARG_subscribed, ARG_unsubscribed, ARG_published };
    mqtt_obj_t *self = pos_args[0];
    if (checkClient(self)) return mp_const_none;

	const mp_arg_t mqtt_config_allowed_args[] = {
			{ MP_QSTR_clientid,     	MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
			{ MP_QSTR_autoreconnect,   	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },
			{ MP_QSTR_cleansession, 	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },
			{ MP_QSTR_keepalive,    	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },
			{ MP_QSTR_qos,          	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },
			{ MP_QSTR_retain,       	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },
			{ MP_QSTR_secure,       	MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = -1} },
			{ MP_QSTR_data_cb,     		MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
			{ MP_QSTR_connected_cb,  	MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
			{ MP_QSTR_disconnected_cb,	MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
			{ MP_QSTR_subscribed_cb,  	MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
			{ MP_QSTR_unsubscribed_cb, 	MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
			{ MP_QSTR_published_cb,		MP_ARG_KW_ONLY  | MP_ARG_OBJ, {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mqtt_config_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mqtt_config_allowed_args), mqtt_config_allowed_args, args);

    if (args[ARG_secure].u_int >= 0) self->client->settings->use_ssl = args[ARG_secure].u_bool;
    if (MP_OBJ_IS_STR(args[ARG_clientid].u_obj)) {
        snprintf(self->client->settings->client_id, CONFIG_MQTT_MAX_CLIENT_LEN, mp_obj_str_get_str(args[ARG_clientid].u_obj));
    }
    if (args[ARG_reconnect].u_int >= 0) self->client->settings->auto_reconnect = args[ARG_reconnect].u_int;
    if (args[ARG_keepalive].u_int >= 0) self->client->settings->keepalive = args[ARG_keepalive].u_int;
    if (args[ARG_qos].u_int >= 0) self->client->settings->lwt_qos = args[ARG_qos].u_int;
    if (args[ARG_retain].u_int >= 0) self->client->settings->lwt_retain = args[ARG_retain].u_int;
    if (args[ARG_cleansess].u_int >= 0) self->client->settings->clean_session = args[ARG_cleansess].u_int;

    if ((MP_OBJ_IS_FUN(args[ARG_datacb].u_obj)) || (MP_OBJ_IS_METH(args[ARG_datacb].u_obj))) {
	    self->client->settings->data_cb = NULL;
	    self->client->settings->mpy_data_cb = args[ARG_datacb].u_obj;
	    self->client->settings->data_cb = (void*)data_cb;
	}
    if ((MP_OBJ_IS_FUN(args[ARG_connected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_connected].u_obj))) {
	    self->client->settings->connected_cb = NULL;
	    self->client->settings->mpy_connected_cb = args[ARG_connected].u_obj;
	    self->client->settings->connected_cb = (void*)connected_cb;
	}
    if ((MP_OBJ_IS_FUN(args[ARG_disconnected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_disconnected].u_obj))) {
	    self->client->settings->disconnected_cb = NULL;
	    self->client->settings->mpy_disconnected_cb = args[ARG_disconnected].u_obj;
	    self->client->settings->disconnected_cb = (void*)disconnected_cb;
	}
    if ((MP_OBJ_IS_FUN(args[ARG_subscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_subscribed].u_obj))) {
	    self->client->settings->subscribe_cb = NULL;
	    self->client->settings->mpy_subscribed_cb = args[ARG_subscribed].u_obj;
	    self->client->settings->subscribe_cb = (void*)subscribed_cb;
	}
    if ((MP_OBJ_IS_FUN(args[ARG_unsubscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_unsubscribed].u_obj))) {
	    self->client->settings->unsubscribe_cb = NULL;
	    self->client->settings->mpy_unsubscribed_cb = args[ARG_unsubscribed].u_obj;
	    self->client->settings->unsubscribe_cb = (void*)unsubscribed_cb;
	}
    if ((MP_OBJ_IS_FUN(args[ARG_published].u_obj)) || (MP_OBJ_IS_METH(args[ARG_published].u_obj))) {
	    self->client->settings->publish_cb = NULL;
	    self->client->settings->mpy_published_cb = args[ARG_published].u_obj;
	    self->client->settings->publish_cb = (void*)published_cb;
	}

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mqtt_config_obj, 1, mqtt_op_config);

//--------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_subscribe(mp_obj_t self_in, mp_obj_t topic_in)
{
    mqtt_obj_t *self = self_in;
    if (checkClient(self)) return mp_const_false;

    const char *topic = mp_obj_str_get_str(topic_in);
    int wait = 2000;
    mqtt_subscribe(self->client, topic, self->client->settings->lwt_qos);
	while ((wait > 0) && (self->client->subs_flag == 0)) {
		vTaskDelay(10 / portTICK_PERIOD_MS);
		wait -= 10;
	}
	if (wait) return mp_const_true;
	else return mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_2(mqtt_subscribe_obj, mqtt_op_subscribe);

//----------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_unsubscribe(mp_obj_t self_in, mp_obj_t topic_in)
{
    mqtt_obj_t *self = self_in;
    if (checkClient(self)) return mp_const_false;

    const char *topic = mp_obj_str_get_str(topic_in);
    int wait = 2000;
    mqtt_unsubscribe(self->client, topic);
	while ((wait > 0) && (self->client->unsubs_flag == 0)) {
		vTaskDelay(10 / portTICK_PERIOD_MS);
		wait -= 10;
	}
	if (wait) return mp_const_true;
	else return mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_2(mqtt_unsubscribe_obj, mqtt_op_unsubscribe);

//-----------------------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_publish(mp_obj_t self_in, mp_obj_t topic_in, mp_obj_t msg_in)
{
    mqtt_obj_t *self = self_in;
    if (checkClient(self)) return mp_const_false;

    size_t len;
    const char *topic = mp_obj_str_get_str(topic_in);
    const char *msg = mp_obj_str_get_data(msg_in, &len);
    int res = mqtt_publish(self->client, topic, msg, len, self->client->settings->lwt_qos, self->client->settings->lwt_retain);

    if (res < 0) return mp_const_false;
    return mp_const_true;
}
MP_DEFINE_CONST_FUN_OBJ_3(mqtt_publish_obj, mqtt_op_publish);

//----------------------------------------------
STATIC mp_obj_t mqtt_op_status(mp_obj_t self_in)
{
    mqtt_obj_t *self = self_in;
    checkClient(self);

    char sstat[16];
	mp_obj_t tuple[2];

    if (self->client == NULL) {
    	tuple[0] = mp_obj_new_int(-1);
        sprintf(sstat, "Destroyed");
    }
    else {
		tuple[0] = mp_obj_new_int(self->client->status);
	    if (self->client->status == MQTT_STATUS_CONNECTED) sprintf(sstat, "Connected");
	    else if (self->client->status == MQTT_STATUS_DISCONNECTED) sprintf(sstat, "Disconnected");
	    else if (self->client->status == MQTT_STATUS_STOPPING) sprintf(sstat, "Stopping");
	    else if (self->client->status == MQTT_STATUS_STOPPED) sprintf(sstat, "Stopped");
	    else sprintf(sstat, "Unknown");
    }
	tuple[1] = mp_obj_new_str(sstat, strlen(sstat), 0);

	return mp_obj_new_tuple(2, tuple);
}
MP_DEFINE_CONST_FUN_OBJ_1(mqtt_status_obj, mqtt_op_status);

//--------------------------------------------
STATIC mp_obj_t mqtt_op_stop(mp_obj_t self_in)
{
    mqtt_obj_t *self = self_in;
    int status = checkClient(self);

    if (status < 2) {
		mqtt_stop(self->client);
		vTaskDelay(100 / portTICK_RATE_MS);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mqtt_stop_obj, mqtt_op_stop);

//--------------------------------------------
STATIC mp_obj_t mqtt_op_start(mp_obj_t self_in)
{
    mqtt_obj_t *self = self_in;

	if ((self->client) && (self->client->status == MQTT_STATUS_STOPPED) && (self->client->settings->xMqttTask == NULL)) {
	    int res = mqtt_start(self->client);
	    if (res != 0) {
	    	free(self->client->settings);
	    	free(self->client);
	        nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "Error starting client"));
	    }
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mqtt_start_obj, mqtt_op_start);

//--------------------------------------------
STATIC mp_obj_t mqtt_op_free(mp_obj_t self_in)
{
    mqtt_obj_t *self = self_in;
	if ((self->client) && (self->client->status == MQTT_STATUS_STOPPED) && (self->client->settings->xMqttTask == NULL)) {
    	free(self->client->settings);
    	free(self->client);
    	self->client = NULL;
        return mp_const_true;
    }
    return mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_1(mqtt_free_obj, mqtt_op_free);


//=========================================================
STATIC const mp_rom_map_elem_t mqtt_locals_dict_table[] = {
	    { MP_ROM_QSTR(MP_QSTR_config),		(mp_obj_t)&mqtt_config_obj },
	    { MP_ROM_QSTR(MP_QSTR_subscribe),	(mp_obj_t)&mqtt_subscribe_obj },
	    { MP_ROM_QSTR(MP_QSTR_unsubscribe),	(mp_obj_t)&mqtt_unsubscribe_obj },
	    { MP_ROM_QSTR(MP_QSTR_publish),		(mp_obj_t)&mqtt_publish_obj },
	    { MP_ROM_QSTR(MP_QSTR_status),		(mp_obj_t)&mqtt_status_obj },
	    { MP_ROM_QSTR(MP_QSTR_stop),		(mp_obj_t)&mqtt_stop_obj },
	    { MP_ROM_QSTR(MP_QSTR_start),		(mp_obj_t)&mqtt_start_obj },
	    { MP_ROM_QSTR(MP_QSTR_free),		(mp_obj_t)&mqtt_free_obj },
};
STATIC MP_DEFINE_CONST_DICT(mqtt_locals_dict, mqtt_locals_dict_table);

//===============================
const mp_obj_type_t mqtt_type = {
    { &mp_type_type },
    .name = MP_QSTR_Mqtt,
    .print = mqtt_print,
    .make_new = mqtt_make_new,
    .locals_dict = (mp_obj_dict_t*)&mqtt_locals_dict,
};

#endif

