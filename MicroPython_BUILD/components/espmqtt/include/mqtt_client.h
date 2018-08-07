/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Apache License Version 2.0
 *
 * Slightly modified mqtt library from https://github.com/tuanpmt/espmqtt
 *
 * Copyright (c) 2018 tuanpm (https://github.com/tuanpmt/espmqtt)
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
*/

/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */

#ifndef _MQTT_CLIENT_H_
#define _MQTT_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

#include "platform.h"
#include "mqtt_config.h"
#include "mqtt_msg.h"
#include "transport.h"
#include "transport_tcp.h"
#include "transport_ssl.h"
#include "transport_ws.h"
#include "platform.h"
#include "mqtt_outbox.h"

typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;

typedef enum {
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
} esp_mqtt_event_id_t;

typedef enum {
    MQTT_TRANSPORT_UNKNOWN = 0x0,
    MQTT_TRANSPORT_OVER_TCP,
    MQTT_TRANSPORT_OVER_SSL,
    MQTT_TRANSPORT_OVER_WS,
    MQTT_TRANSPORT_OVER_WSS
} esp_mqtt_transport_t;

typedef enum {
    MQTT_STATE_ERROR = -1,
    MQTT_STATE_UNKNOWN = 0,
    MQTT_STATE_INIT,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_WAIT_TIMEOUT,
} mqtt_client_state_t;

typedef struct {
    esp_mqtt_event_id_t event_id;
    esp_mqtt_client_handle_t client;
    void *user_context;
    char *data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char *topic;
    int topic_len;
    int msg_id;
    int type;
} esp_mqtt_event_t;

typedef esp_mqtt_event_t* esp_mqtt_event_handle_t;

typedef esp_err_t (* mqtt_event_callback_t)(esp_mqtt_event_handle_t event);


typedef struct {
    mqtt_event_callback_t event_handle;
    char host[MQTT_MAX_HOST_LEN];
    char uri[MQTT_MAX_HOST_LEN];
    uint32_t port;
    char client_id[MQTT_MAX_CLIENT_LEN];
    char username[MQTT_MAX_USERNAME_LEN];
    char password[MQTT_MAX_PASSWORD_LEN];
    char lwt_topic[MQTT_MAX_LWT_TOPIC];
    char lwt_msg[MQTT_MAX_LWT_MSG];
    int lwt_qos;
    int lwt_retain;
    int lwt_msg_len;
    int disable_clean_session;
    int keepalive;
    bool disable_auto_reconnect;
    void *user_context;
    int task_prio;
    int task_stack;
    int buffer_size;
    const char *cert_pem;
    esp_mqtt_transport_t transport;
} esp_mqtt_client_config_t;

typedef struct mqtt_state
{
    mqtt_connect_info_t *connect_info;
    uint8_t *in_buffer;
    uint8_t *out_buffer;
    int in_buffer_length;
    int out_buffer_length;
    uint16_t message_length;
    uint16_t message_length_read;
    mqtt_message_t *outbound_message;
    mqtt_connection_t mqtt_connection;
    uint16_t pending_msg_id;
    int pending_msg_type;
    int pending_publish_qos;
    int pending_msg_count;
} mqtt_state_t;

typedef struct {
    mqtt_event_callback_t event_handle;
    int task_stack;
    int task_prio;
    char *uri;
    char *host;
    char *path;
    char *scheme;
    int port;
    bool auto_reconnect;
    void *user_context;
    int network_timeout_ms;
} mqtt_config_storage_t;

struct esp_mqtt_client {
    transport_list_handle_t transport_list;
    transport_handle_t transport;
    mqtt_config_storage_t *config;
    mqtt_state_t  mqtt_state;
    mqtt_connect_info_t connect_info;
    mqtt_client_state_t state;
    long long keepalive_tick;
    long long reconnect_tick;
    int wait_timeout_ms;
    int auto_reconnect;
    esp_mqtt_event_t event;
    bool run;
    outbox_handle_t outbox;
    EventGroupHandle_t status_bits;
    void *mpy_mqtt_obj;
};

extern const char *MQTT_TAG;

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
esp_err_t esp_mqtt_client_set_uri(esp_mqtt_client_handle_t client, const char *uri);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos);
esp_err_t esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, const char *topic);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);

#endif
