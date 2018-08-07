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

#include "mqtt_client.h"
#include "http_parser.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "mphalport.h"
#include "extmod/vfs_native.h"

#define CONFIG_MQTT_MAX_TASKNAME_LEN	16

typedef struct _mqtt_obj_t {
    mp_obj_base_t base;
    esp_mqtt_client_handle_t client;
    esp_mqtt_client_config_t mqtt_cfg;
    char name[CONFIG_MQTT_MAX_TASKNAME_LEN];
    void *mpy_connected_cb;
    void *mpy_disconnected_cb;
    void *mpy_subscribed_cb;
    void *mpy_unsubscribed_cb;
    void *mpy_published_cb;
    void *mpy_data_cb;
    uint8_t *msgbuf;
    uint8_t *topicbuf;
    char *certbuf;
    uint8_t subs_flag;
    uint8_t unsubs_flag;
    uint8_t publish_flag;
} mqtt_obj_t;

const mp_obj_type_t mqtt_type;



//------------------------------------------
STATIC int checkClient(mqtt_obj_t *mqtt_obj)
{
	if (mqtt_obj->client == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Mqtt client destroyed"));
    }
	return mqtt_obj->client->state;
}

//----------------------------------------
STATIC void connected_cb(mqtt_obj_t *self)
{
    if (self->mpy_connected_cb) {
		mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_SINGLE);
		if (!carg) return;
		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(self->name), (const uint8_t *)self->name, NULL)) return;
		mp_sched_schedule(self->mpy_connected_cb, mp_const_none, carg);
    }
}

//-------------------------------------------
STATIC void disconnected_cb(mqtt_obj_t *self)
{
    if (self->mpy_disconnected_cb) {
		mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_SINGLE);
		if (!carg) return;
		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(self->name), (const uint8_t *)self->name, NULL)) return;
		mp_sched_schedule(self->mpy_disconnected_cb, mp_const_none, carg);
    }
}

//------------------------------------------------------------
STATIC void subscribed_cb(mqtt_obj_t *self, const char *topic)
{
    if (self->mpy_subscribed_cb) {
    	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    	if (carg == NULL) return;
   		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(self->name), (const uint8_t *)self->name, NULL)) return;
   		if (topic) {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen(topic), (const uint8_t *)topic, NULL)) return;
   		}
   		else {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, 1, (const uint8_t *)"?", NULL)) return;
   		}
    	mp_sched_schedule(self->mpy_subscribed_cb, mp_const_none, carg);
    }
}

//--------------------------------------------------------------
STATIC void unsubscribed_cb(mqtt_obj_t *self, const char *topic)
{
    if (self->mpy_unsubscribed_cb) {
    	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    	if (carg == NULL) return;
   		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(self->name), (const uint8_t *)self->name, NULL)) return;
   		if (topic) {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen(topic), (const uint8_t *)topic, NULL)) return;
   		}
   		else {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, 1, (const uint8_t *)"?", NULL)) return;
   		}
    	mp_sched_schedule(self->mpy_unsubscribed_cb, mp_const_none, carg);
    }
}

//---------------------------------------------------------------------
STATIC void published_cb(mqtt_obj_t *self, const char *topic, int type)
{
    if (self->mpy_published_cb) {
    	mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
    	if (carg == NULL) return;
   		if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(self->name), (const uint8_t *)self->name, NULL)) return;
   		if (topic) {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen(topic), (const uint8_t *)topic, NULL)) return;
   		}
   		else {
   	   		if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, 1, (const uint8_t *)"?", NULL)) return;
   		}
   		if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_INT, type, NULL, NULL)) return;
    	mp_sched_schedule(self->mpy_published_cb, mp_const_none, carg);
    }
}

//-------------------------------------------------
STATIC void data_cb(mqtt_obj_t *self, void *params)
{
    if (!self->mpy_data_cb) return;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)params;

	if (event->current_data_offset == 0) {
		// *** First block of data
		if (self->msgbuf != NULL) free(self->msgbuf);
		if (self->topicbuf != NULL) free(self->topicbuf);
		self->msgbuf = NULL;
		self->topicbuf = NULL;
		if (event->data_len < event->total_data_len) {
			// === more data will follow, allocate the data buffer and copy the first part ===
			self->topicbuf = malloc(event->topic_len + 1);
			if (self->topicbuf) {
				memcpy(self->topicbuf, event->topic, event->topic_len);
				self->topicbuf[event->topic_len] = 0;

				int buf_len = event->total_data_len + 1;
				self->msgbuf = malloc(buf_len + 1);
				if (self->msgbuf) {
					memcpy(self->msgbuf, event->data, event->data_len);
					self->msgbuf[event->data_len] = 0;
				}
				else {
					free(self->topicbuf);
					self->msgbuf = NULL;
					self->topicbuf = NULL;
				}
			}
		}
		else {
			// === all data received, we can schedule the callback function now ===
			mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
			if (!carg) return;
			if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(self->name), (const uint8_t *)self->name, NULL)) return;
			if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, event->topic_len, (const uint8_t *)event->topic, NULL)) return;
			if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, event->data_len, (const uint8_t *)event->data, NULL)) return;
			mp_sched_schedule(self->mpy_data_cb, mp_const_none, carg);
		}
	}
	else {
		if ((self->topicbuf) && (self->msgbuf)) {
			// === more payload data arrived, add to buffer ===
			int new_len = event->current_data_offset + event->data_len;
			memcpy(self->msgbuf + event->current_data_offset, event->data, event->data_len);
			self->msgbuf[new_len] = 0;
			if (new_len >= event->total_data_len) {
				// === all data received, we can schedule the callback function now ===
				mp_sched_carg_t *carg = make_cargs(MP_SCHED_CTYPE_TUPLE);
				if (!carg) goto freebufs;
				if (!make_carg_entry(carg, 0, MP_SCHED_ENTRY_TYPE_STR, strlen(self->name), (const uint8_t *)self->name, NULL)) goto freebufs;
				if (!make_carg_entry(carg, 1, MP_SCHED_ENTRY_TYPE_STR, strlen((const char *)self->topicbuf), self->topicbuf, NULL)) goto freebufs;
				if (!make_carg_entry(carg, 2, MP_SCHED_ENTRY_TYPE_STR, event->total_data_len, self->msgbuf, NULL)) goto freebufs;
				mp_sched_schedule(self->mpy_data_cb, mp_const_none, carg);
freebufs:
				// Free the buffers
				free(self->msgbuf);
				free(self->topicbuf);
				self->msgbuf = NULL;
				self->topicbuf = NULL;
			}
		}
		else {
			// more payload data arrived, but there is no data buffers allocated (!?)
			if (self->msgbuf != NULL) free(self->msgbuf);
			if (self->topicbuf != NULL) free(self->topicbuf);
			self->msgbuf = NULL;
			self->topicbuf = NULL;
		}
	}
}

//----------------------------------------------------------------
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    mqtt_obj_t *mpy_client = (mqtt_obj_t *)client->mpy_mqtt_obj;
	const char *topic = NULL;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_TAG, "Connected");
        	connected_cb(mpy_client);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(MQTT_TAG, "Disconnected");
        	disconnected_cb(mpy_client);
            break;

        case MQTT_EVENT_SUBSCRIBED:
        	if (client->config->user_context) {
        		topic = (const char *)client->config->user_context;
                ESP_LOGI(MQTT_TAG, "Subscribed to '%s'", topic);
        	}
        	else {
                ESP_LOGI(MQTT_TAG, "Subscribed");
        	}
        	subscribed_cb(mpy_client, topic);
        	mpy_client->subs_flag = 1;
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
        	if (client->config->user_context) {
        		topic = (const char *)client->config->user_context;
                ESP_LOGI(MQTT_TAG, "Unsubscribed from '%s'", topic);
        	}
        	else {
                ESP_LOGI(MQTT_TAG, "Unsubscribed");
        	}
        	unsubscribed_cb(mpy_client, topic);
        	mpy_client->unsubs_flag = 1;
            break;
        case MQTT_EVENT_PUBLISHED:
        	if (client->config->user_context) {
        		topic = (const char *)client->config->user_context;
                ESP_LOGI(MQTT_TAG, "Published to '%s'", topic);
        	}
        	else {
                ESP_LOGI(MQTT_TAG, "Published");
        	}
        	published_cb(mpy_client, topic, event->type);
        	mpy_client->publish_flag = 1;
            break;
        case MQTT_EVENT_DATA:
        	if (mpy_client->mpy_data_cb == NULL) {
        		ESP_LOGI(MQTT_TAG, "TOPIC: %.*s\r\n", event->topic_len, event->topic);
        		ESP_LOGI(MQTT_TAG, " DATA: %.*s\r\n", event->data_len, event->data);
        	}
        	else {
        		ESP_LOGI(MQTT_TAG, "Data received");
        	}
        	data_cb(mpy_client, event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTT_TAG, "Mqtt Error");
            break;
    }
    return ESP_OK;
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
    if (self->client->state == MQTT_STATE_CONNECTED) sprintf(sstat, "Connected");
    else if (self->client->state == MQTT_STATE_INIT) sprintf(sstat, "Initialized");
    else if (self->client->state == MQTT_STATE_WAIT_TIMEOUT) sprintf(sstat, "Wait timeout");
    else if (self->client->state == MQTT_STATE_UNKNOWN) sprintf(sstat, "Unknown");
    else sprintf(sstat, "Error");

    const char *server_uri = "Unknown";
    if (self->client->config->uri) server_uri = self->client->config->uri;
    else if (self->client->config->host) server_uri = self->client->config->host;

    mp_printf(print, "Mqtt[%s]\n    (Server: %s:%u, Status: %s\n", self->name, server_uri, self->client->config->port, sstat);
    if ((self->client->state == MQTT_STATE_CONNECTED) || (self->client->state == MQTT_STATE_INIT)) {
		mp_printf(print, "     Client ID: %s, Clean session=%s, Keepalive=%ds\n     LWT(",
				self->client->connect_info.client_id, (self->client->connect_info.clean_session ? "True" : "False"), self->client->connect_info.keepalive);
		if (self->client->connect_info.will_topic) {
			mp_printf(print, "QoS=%d, Retain=%s, Topic='%s', Msg='%s')\n",
					self->client->connect_info.will_qos, (self->client->connect_info.will_retain ? "True" : "False"), self->client->connect_info.will_topic, self->client->connect_info.will_message);
		}
		else mp_printf(print, "not set)\n");
    }
    /*
	if ((self->client->settings->xMqttTask) && (self->client->settings->xMqttSendingTask)) {
		mp_printf(print, "     Used stack: %u/%u + %u/%u\n",
				self->client->settings->xMqttTask_stacksize - uxTaskGetStackHighWaterMark(self->client->settings->xMqttTask), self->client->settings->xMqttTask_stacksize,
				self->client->settings->xMqttSendingTask_stacksize - uxTaskGetStackHighWaterMark(self->client->settings->xMqttSendingTask), self->client->settings->xMqttSendingTask_stacksize);
	}
	*/
	mp_printf(print, "    )\n");
}


//------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t mqtt_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	enum { ARG_name, ARG_server, ARG_user, ARG_pass, ARG_port, ARG_reconnect, ARG_clientid, ARG_cleansess, ARG_keepalive, ARG_cert,
		ARG_lwt_topic, ARG_lwt_msg, ARG_lwt_qos, ARG_lwt_retain, ARG_datacb, ARG_connected, ARG_disconnected, ARG_subscribed, ARG_unsubscribed, ARG_published };

    const mp_arg_t mqtt_init_allowed_args[] = {
			{ MP_QSTR_name,   	    	MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_server,       	MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_user,         	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_password,        	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_port,         	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_autoreconnect,   	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_clientid,     	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_cleansession, 	MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false} },
			{ MP_QSTR_keepalive,    	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = MQTT_KEEPALIVE_TICK} },
			{ MP_QSTR_cert,       		MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_lwt_topic,       	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_lwt_msg,       	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_lwt_qos,         	MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_lwt_retain,	    MP_ARG_KW_ONLY  | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_data_cb,       	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_connected_cb,  	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_disconnected_cb,	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_subscribed_cb,  	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_unsubscribed_cb, 	MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_published_cb,		MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(mqtt_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(mqtt_init_allowed_args), mqtt_init_allowed_args, args);

	const char *tstr = NULL;

	// Create the mqtt object
    mqtt_obj_t *self = m_new_obj(mqtt_obj_t );
    memset(self, 0 , sizeof(mqtt_obj_t));

    // Populate settings
    esp_mqtt_client_config_t mqtt_cfg = {0};

    // Event handle
    mqtt_cfg.event_handle = mqtt_event_handler;

    // Object name
    tstr = mp_obj_str_get_str(args[ARG_name].u_obj);
    if (strlen(tstr) >= CONFIG_MQTT_MAX_TASKNAME_LEN) {
		mp_raise_ValueError("Name too long");
    }
    sprintf(self->name, "%s", tstr);

    // Port
    if (args[ARG_port].u_int > 0) mqtt_cfg.port = args[ARG_port].u_int;

    // Get URI or server domain
    tstr = mp_obj_str_get_str(args[ARG_server].u_obj);
    if (strlen(tstr) >= MQTT_MAX_HOST_LEN) {
		mp_raise_ValueError("URI too long");
    }
    struct http_parser_url puri;
    http_parser_url_init(&puri);
    int parser_status = http_parser_parse_url(tstr, strlen(tstr), 0, &puri);
    if (parser_status != 0) {
    	sprintf(mqtt_cfg.host, "%s", tstr);
    }
    else sprintf(mqtt_cfg.uri, "%s", tstr);

    // User name
    if (MP_OBJ_IS_STR(args[ARG_user].u_obj)) {
        tstr = mp_obj_str_get_str(args[ARG_user].u_obj);
        if (strlen(tstr) >= MQTT_MAX_USERNAME_LEN) {
    		mp_raise_ValueError("User name too long");
        }
        sprintf(mqtt_cfg.username, "%s", tstr);
    }
    // Password
    if (MP_OBJ_IS_STR(args[ARG_pass].u_obj)) {
        tstr = mp_obj_str_get_str(args[ARG_pass].u_obj);
        if (strlen(tstr) >= MQTT_MAX_PASSWORD_LEN) {
    		mp_raise_ValueError("Password too long");
        }
        sprintf(mqtt_cfg.password, "%s", tstr);
    }
    // Client ID
    if (MP_OBJ_IS_STR(args[ARG_clientid].u_obj)) {
        tstr = mp_obj_str_get_str(args[ARG_clientid].u_obj);
        if (strlen(tstr) >= MQTT_MAX_CLIENT_LEN) {
    		mp_raise_ValueError("Client ID too long");
        }
        sprintf(mqtt_cfg.client_id, "%s", tstr);
    }
    else sprintf(mqtt_cfg.client_id, "mpy_mqtt_client");

    mqtt_cfg.disable_auto_reconnect = args[ARG_reconnect].u_int ? false : true;
    mqtt_cfg.keepalive = args[ARG_keepalive].u_int;
    mqtt_cfg.disable_clean_session = args[ARG_cleansess].u_int ? false : true;

    // LWT options
    if (MP_OBJ_IS_STR(args[ARG_lwt_topic].u_obj)) {
        tstr = mp_obj_str_get_str(args[ARG_lwt_topic].u_obj);
        if (strlen(tstr) >= MQTT_MAX_LWT_TOPIC) {
    		mp_raise_ValueError("LWT topic too long");
        }
        sprintf(mqtt_cfg.lwt_topic, "%s", tstr);
        if (MP_OBJ_IS_STR(args[ARG_lwt_msg].u_obj)) {
            tstr = mp_obj_str_get_str(args[ARG_lwt_msg].u_obj);
            if (strlen(tstr) >= MQTT_MAX_LWT_MSG) {
        		mp_raise_ValueError("LWT message too long");
            }
            sprintf(mqtt_cfg.lwt_msg, "%s", tstr);
        }
        else sprintf(mqtt_cfg.lwt_msg, "offline");
		mqtt_cfg.lwt_qos = args[ARG_lwt_qos].u_int;
		mqtt_cfg.lwt_retain = args[ARG_lwt_retain].u_int;
    }

    // Get Certificate from file
    if (MP_OBJ_IS_STR(args[ARG_cert].u_obj)) {
        char *fname = NULL;
        char fullname[128] = {'\0'};

        fname = (char *)mp_obj_str_get_str(args[ARG_cert].u_obj);

        int res = physicalPath(fname, fullname);
        if ((res != 0) || (strlen(fullname) == 0)) {
    		mp_raise_ValueError("Certificate file not found");
        }
    	struct stat sb;
    	if (stat(fullname, &sb) != 0) {
    		mp_raise_ValueError("Error opening certificate file");
    	}
    	int size = sb.st_size;
    	FILE *fhndl = fopen(fullname, "rb");
        if (fhndl != NULL) {
        	if (self->certbuf) {
        		free(self->certbuf);
        		self->certbuf = NULL;
        	}
        	self->certbuf = malloc(size);
        	if (self->certbuf == NULL) {
            	fclose(fhndl);
        		mp_raise_ValueError("Error allocating certification buffer");
        	}
    		int len = fread(self->certbuf, 1, size, fhndl);  // read cert file
        	fclose(fhndl);
    		if (len != size) {
        		free(self->certbuf);
        		self->certbuf = NULL;
        		mp_raise_ValueError("Error reading certificate file");
    		}
        	mqtt_cfg.cert_pem = (const char *)self->certbuf;
        }
        else {
    		mp_raise_ValueError("Error opening certificate file");
        }
    }

    // Set callbacks
    if ((MP_OBJ_IS_FUN(args[ARG_datacb].u_obj)) || (MP_OBJ_IS_METH(args[ARG_datacb].u_obj))) {
	    self->mpy_data_cb = args[ARG_datacb].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_connected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_connected].u_obj))) {
	    self->mpy_connected_cb = args[ARG_connected].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_disconnected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_disconnected].u_obj))) {
	    self->mpy_disconnected_cb = args[ARG_disconnected].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_subscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_subscribed].u_obj))) {
	    self->mpy_subscribed_cb = args[ARG_subscribed].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_unsubscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_unsubscribed].u_obj))) {
	    self->mpy_unsubscribed_cb = args[ARG_unsubscribed].u_obj;
	}

    if ((MP_OBJ_IS_FUN(args[ARG_published].u_obj)) || (MP_OBJ_IS_METH(args[ARG_published].u_obj))) {
	    self->mpy_published_cb = args[ARG_published].u_obj;
	}

    self->base.type = &mqtt_type;

    self->client = esp_mqtt_client_init(&mqtt_cfg);
    if (self->client == NULL) {
		mp_raise_ValueError("Error initializing mqtt client");
    }

    self->client->mpy_mqtt_obj = self;
    //esp_mqtt_client_start(self->client);

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_config(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_server, ARG_user, ARG_pass, ARG_port, ARG_reconnect, ARG_clientid, ARG_cleansess, ARG_keepalive, ARG_lwt_topic, ARG_lwt_msg,
		   ARG_lwt_qos, ARG_lwt_retain, ARG_datacb, ARG_connected, ARG_disconnected, ARG_subscribed, ARG_unsubscribed, ARG_published };

    const mp_arg_t mqtt_config_allowed_args[] = {
			{ MP_QSTR_server,       	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_user,         	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_password,        	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_port,         	MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_autoreconnect,   	MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_clientid,     	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_cleansession, 	MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_keepalive,    	MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
			{ MP_QSTR_lwt_topic,       	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_lwt_msg,       	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_lwt_qos,         	MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_lwt_retain,	    MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 0} },
			{ MP_QSTR_data_cb,       	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_connected_cb,  	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_disconnected_cb,	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_subscribed_cb,  	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_unsubscribed_cb, 	MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_published_cb,		MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};

    mqtt_obj_t *self = pos_args[0];
	if (!self->client) {
		mp_raise_ValueError("Destroyed");
	}
    if (self->client->state < MQTT_STATE_INIT) {
		mp_raise_ValueError("Not initialized");
    }

    mp_arg_val_t args[MP_ARRAY_SIZE(mqtt_config_allowed_args)];
    mp_arg_parse_all(n_args-1, pos_args+1, kw_args, MP_ARRAY_SIZE(mqtt_config_allowed_args), mqtt_config_allowed_args, args);

    const char *tstr = NULL;
    bool has_conn_arg = false;
    if ((MP_OBJ_IS_STR(args[ARG_server].u_obj)) || (args[ARG_port].u_int > 0) || (MP_OBJ_IS_STR(args[ARG_user].u_obj)) ||
    		(MP_OBJ_IS_STR(args[ARG_pass].u_obj)) || (MP_OBJ_IS_STR(args[ARG_clientid].u_obj)) ||
			(args[ARG_reconnect].u_int >= 0) || (args[ARG_keepalive].u_int > 0) || (args[ARG_cleansess].u_int >= 0) ||
			(MP_OBJ_IS_STR(args[ARG_lwt_topic].u_obj)) ) has_conn_arg = true;

	if ((has_conn_arg) && (self->client->state < MQTT_STATE_CONNECTED)) {
    	// not started, we can change all parameters
        // URI
        if (MP_OBJ_IS_STR(args[ARG_server].u_obj)) {
			tstr = mp_obj_str_get_str(args[ARG_server].u_obj);
			if (strlen(tstr) >= MQTT_MAX_HOST_LEN) {
				mp_raise_ValueError("URI too long");
			}
			sprintf(self->client->config->uri, "%s", tstr);
        }

        // Port
        if (args[ARG_port].u_int > 0) self->client->config->port = args[ARG_port].u_int;

        // User name
        if (MP_OBJ_IS_STR(args[ARG_user].u_obj)) {
            tstr = mp_obj_str_get_str(args[ARG_user].u_obj);
            if (strlen(tstr) >= MQTT_MAX_USERNAME_LEN) {
        		mp_raise_ValueError("User name too long");
            }
            sprintf(self->client->connect_info.username, "%s", tstr);
        }
        // Password
        if (MP_OBJ_IS_STR(args[ARG_pass].u_obj)) {
            tstr = mp_obj_str_get_str(args[ARG_pass].u_obj);
            if (strlen(tstr) >= MQTT_MAX_PASSWORD_LEN) {
        		mp_raise_ValueError("Password too long");
            }
            sprintf(self->client->connect_info.password, "%s", tstr);
        }
        // Client ID
        if (MP_OBJ_IS_STR(args[ARG_clientid].u_obj)) {
            tstr = mp_obj_str_get_str(args[ARG_clientid].u_obj);
            if (strlen(tstr) >= MQTT_MAX_CLIENT_LEN) {
        		mp_raise_ValueError("Client ID too long");
            }
            sprintf(self->client->connect_info.client_id, "%s", tstr);
        }
        else sprintf(self->client->connect_info.client_id, "mpy_mqtt_client");

        if (args[ARG_reconnect].u_int >= 0) self->client->config->auto_reconnect = args[ARG_reconnect].u_int ? true : false;
        if (args[ARG_keepalive].u_int > 0) self->client->connect_info.keepalive = args[ARG_keepalive].u_int;
        if (args[ARG_cleansess].u_int >= 0) self->client->connect_info.clean_session = args[ARG_cleansess].u_int ? true : false;

        // LWT options
        if (MP_OBJ_IS_STR(args[ARG_lwt_topic].u_obj)) {
            tstr = mp_obj_str_get_str(args[ARG_lwt_topic].u_obj);
            if (strlen(tstr) >= MQTT_MAX_LWT_TOPIC) {
        		mp_raise_ValueError("LWT topic too long");
            }
            sprintf(self->client->connect_info.will_topic, "%s", tstr);
            if (MP_OBJ_IS_STR(args[ARG_lwt_msg].u_obj)) {
                tstr = mp_obj_str_get_str(args[ARG_lwt_msg].u_obj);
                if (strlen(tstr) >= MQTT_MAX_LWT_MSG) {
            		mp_raise_ValueError("LWT message too long");
                }
                sprintf(self->client->connect_info.will_topic, "%s", tstr);
            }
            if (args[ARG_lwt_qos].u_int >= 0) self->client->connect_info.will_qos = args[ARG_lwt_qos].u_int;
            if (args[ARG_lwt_retain].u_int >= 0) self->client->connect_info.will_retain = args[ARG_lwt_retain].u_int;
        }
    }

    // Callbacks
    if ((MP_OBJ_IS_FUN(args[ARG_datacb].u_obj)) || (MP_OBJ_IS_METH(args[ARG_datacb].u_obj))) {
	    self->mpy_data_cb = NULL;
	    self->mpy_data_cb = args[ARG_datacb].u_obj;
	}
    else if (args[ARG_datacb].u_obj == mp_const_false) self->mpy_data_cb = NULL;

    if ((MP_OBJ_IS_FUN(args[ARG_connected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_connected].u_obj))) {
	    self->mpy_connected_cb = NULL;
	    self->mpy_connected_cb = args[ARG_connected].u_obj;
	}
    else if (args[ARG_connected].u_obj == mp_const_false) self->mpy_connected_cb = NULL;

    if ((MP_OBJ_IS_FUN(args[ARG_disconnected].u_obj)) || (MP_OBJ_IS_METH(args[ARG_disconnected].u_obj))) {
	    self->mpy_disconnected_cb = NULL;
	    self->mpy_disconnected_cb = args[ARG_disconnected].u_obj;
	}
    else if (args[ARG_disconnected].u_obj == mp_const_false) self->mpy_disconnected_cb = NULL;

    if ((MP_OBJ_IS_FUN(args[ARG_subscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_subscribed].u_obj))) {
	    self->mpy_subscribed_cb = NULL;
	    self->mpy_subscribed_cb = args[ARG_subscribed].u_obj;
	}
    else if (args[ARG_subscribed].u_obj == mp_const_false) self->mpy_subscribed_cb = NULL;

    if ((MP_OBJ_IS_FUN(args[ARG_unsubscribed].u_obj)) || (MP_OBJ_IS_METH(args[ARG_unsubscribed].u_obj))) {
	    self->mpy_unsubscribed_cb = NULL;
	    self->mpy_unsubscribed_cb = args[ARG_unsubscribed].u_obj;
	}
    else if (args[ARG_unsubscribed].u_obj == mp_const_false) self->mpy_unsubscribed_cb = NULL;

    if ((MP_OBJ_IS_FUN(args[ARG_published].u_obj)) || (MP_OBJ_IS_METH(args[ARG_published].u_obj))) {
	    self->mpy_published_cb = NULL;
	    self->mpy_published_cb = args[ARG_published].u_obj;
	}
    else if (args[ARG_published].u_obj == mp_const_false) self->mpy_published_cb = NULL;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mqtt_config_obj, 1, mqtt_op_config);

//-----------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_subscribe(mp_uint_t n_args, const mp_obj_t *args)
{
    mqtt_obj_t *self = args[0];
    if (checkClient(self) != MQTT_STATE_CONNECTED) return mp_const_false;

    const char *topic = mp_obj_str_get_str(args[1]);
    int wait = 2000;
    int qos = 0;
    if (n_args == 3) {
    	qos = mp_obj_get_int(args[2]);
    	if ((qos < 0) || (qos > 2)) {
    		mp_raise_ValueError("Wrong QoS value");
    	}
    }

    self->subs_flag = 0;
    self->client->config->user_context = (void *)topic;

    int res = esp_mqtt_client_subscribe(self->client, topic, qos);
    if (res < 0) {
    	self->client->config->user_context = NULL;
    	return mp_const_false;
    }
	while ((wait > 0) && (self->subs_flag == 0)) {
		vTaskDelay(10 / portTICK_PERIOD_MS);
		wait -= 10;
	}
	self->client->config->user_context = NULL;
	if (wait) return mp_const_true;
	else return mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mqtt_subscribe_obj, 2, 3, mqtt_op_subscribe);

//----------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_unsubscribe(mp_obj_t self_in, mp_obj_t topic_in)
{
    mqtt_obj_t *self = self_in;
    if (checkClient(self) != MQTT_STATE_CONNECTED) return mp_const_false;

    const char *topic = mp_obj_str_get_str(topic_in);
    int wait = 2000;
    self->unsubs_flag = 0;
    self->client->config->user_context = (void *)topic;

    int res = esp_mqtt_client_unsubscribe(self->client, topic);
    if (res < 0) {
    	self->client->config->user_context = NULL;
    	return mp_const_false;
    }
	while ((wait > 0) && (self->unsubs_flag == 0)) {
		vTaskDelay(10 / portTICK_PERIOD_MS);
		wait -= 10;
	}
	self->client->config->user_context = NULL;
	if (wait) return mp_const_true;
	else return mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_2(mqtt_unsubscribe_obj, mqtt_op_unsubscribe);

//---------------------------------------------------------------------
STATIC mp_obj_t mqtt_op_publish(mp_uint_t n_args, const mp_obj_t *args)
{
    mqtt_obj_t *self = args[0];
    if (checkClient(self) != MQTT_STATE_CONNECTED) return mp_const_false;

    size_t len;
    const char *topic = mp_obj_str_get_str(args[1]);
    const char *msg = mp_obj_str_get_data(args[2], &len);

    int wait = 2000;
    int qos = 0;
    if (n_args == 4) {
    	qos = mp_obj_get_int(args[3]);
    	if ((qos < 0) || (qos > 2)) {
    		mp_raise_ValueError("Wrong QoS value");
    	}
    }
    if (qos == 0) wait = 0;

    int retain = 0;
    if (n_args == 5) retain = mp_obj_is_true(args[4]);

    self->publish_flag = 0;
    self->client->config->user_context = (void *)topic;

    int res = esp_mqtt_client_publish(self->client, topic, msg, len, qos, retain);
    if (res < 0) {
    	self->client->config->user_context = NULL;
    	return mp_const_false;
    }
	while ((wait > 0) && (self->publish_flag == 0)) {
		vTaskDelay(10 / portTICK_PERIOD_MS);
		wait -= 10;
	}
	self->client->config->user_context = NULL;

    return mp_const_true;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mqtt_publish_obj, 3, 5, mqtt_op_publish);

//----------------------------------------------
STATIC mp_obj_t mqtt_op_status(mp_obj_t self_in)
{
    mqtt_obj_t *self = self_in;

    char sstat[16];
	mp_obj_t tuple[2];

    if (self->client == NULL) {
    	tuple[0] = mp_obj_new_int(-1);
        sprintf(sstat, "Destroyed");
    }
    else {
		tuple[0] = mp_obj_new_int(self->client->state);
	    if (self->client->state == MQTT_STATE_CONNECTED) sprintf(sstat, "Connected");
	    else if (self->client->state == MQTT_STATE_INIT) sprintf(sstat, "Initialized");
	    else if (self->client->state == MQTT_STATE_WAIT_TIMEOUT) sprintf(sstat, "Wait timeout");
	    else if (self->client->state == MQTT_STATE_UNKNOWN) sprintf(sstat, "Unknown");
	    else sprintf(sstat, "Error");
    }
	tuple[1] = mp_obj_new_str(sstat, strlen(sstat));

	return mp_obj_new_tuple(2, tuple);
}
MP_DEFINE_CONST_FUN_OBJ_1(mqtt_status_obj, mqtt_op_status);

//--------------------------------------------
STATIC mp_obj_t mqtt_op_stop(mp_obj_t self_in)
{
    mqtt_obj_t *self = self_in;

    if ((self->client) && (self->client->state >= MQTT_STATE_INIT)) {
		esp_mqtt_client_stop(self->client);
		int status = 0;
    	while ((status < 20) && ((xEventGroupGetBits(self->client->status_bits) & 1) == 0)) {
    		vTaskDelay(100 / portTICK_RATE_MS);
    	}
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mqtt_stop_obj, mqtt_op_stop);

//--------------------------------------------
STATIC mp_obj_t mqtt_op_start(mp_obj_t self_in)
{
    mqtt_obj_t *self = self_in;

	if ((self->client) && (self->client->state < MQTT_STATE_INIT)) {
	    int res = esp_mqtt_client_start(self->client);
	    if (res != ESP_OK) {
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

    if (self->client) {
		self->mpy_data_cb = NULL;
		self->mpy_connected_cb = NULL;
		self->mpy_disconnected_cb = NULL;
		self->mpy_subscribed_cb = NULL;
		self->mpy_unsubscribed_cb = NULL;
		self->mpy_published_cb = NULL;

		esp_mqtt_client_destroy(self->client);
    	self->client = NULL;

    	if (self->msgbuf) {
    		free(self->msgbuf);
    		self->msgbuf = NULL;
    	}
    	if (self->topicbuf) {
    		free(self->topicbuf);
    		self->topicbuf = NULL;
    	}
    	if (self->certbuf) {
    		free(self->certbuf);
    		self->certbuf = NULL;
    	}

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

