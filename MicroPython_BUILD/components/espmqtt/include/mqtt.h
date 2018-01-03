/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Boris Lovosevic (https://github.com/loboris)
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

#ifndef _MQTT_H_
#define _MQTT_H_

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_MQTT

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mqtt_msg.h"
#include "ringbuf.h"

#include "openssl/ssl.h"

// Constants not defined in menuconfig
#define CONFIG_MQTT_MAX_HOST_LEN		64
#define CONFIG_MQTT_MAX_CLIENT_LEN		32
#define CONFIG_MQTT_MAX_USERNAME_LEN	32
#define CONFIG_MQTT_MAX_PASSWORD_LEN	32
#define CONFIG_MQTT_MAX_LWT_TOPIC		32
#define CONFIG_MQTT_MAX_LWT_MSG			32
#define CONFIG_MQTT_MAX_TASKNAME_LEN	16

// Mqtt client status constants
#define MQTT_STATUS_DISCONNECTED		0
#define MQTT_STATUS_CONNECTED			1
#define MQTT_STATUS_STOPPING			2
#define MQTT_STATUS_STOPPED				4

#define MQTT_SENDING_TYPE_NONE			0
#define MQTT_SENDING_TYPE_PUBLISH		1
#define MQTT_SENDING_TYPE_SUBSCRIBE		2
#define MQTT_SENDING_TYPE_UNSUBSCRIBE	3
#define MQTT_SENDING_TYPE_PING			4

typedef struct mqtt_client mqtt_client;
typedef struct mqtt_event_data_t mqtt_event_data_t;

/**
 * \return True on connect success, false on error
 */
typedef bool (* mqtt_connect_callback)(mqtt_client *client);
/**
 */
typedef void (* mqtt_disconnect_callback)(mqtt_client *client);
/**
 * \param[out] buffer Pointer to buffer to fill
 * \param[in] len Number of bytes to read
 * \param[in] timeout_ms Time to wait for completion, or 0 for no timeout
 * \return Number of bytes read, less than 0 on error
 */
typedef int (* mqtt_read_callback)(mqtt_client *client, void *buffer, int len, int timeout_ms);
/**
 * \param[in] buffer Pointer to buffer to write
 * \param[in] len Number of bytes to write
 * \param[in] timeout_ms Time to wait for completion, or 0 for no timeout
 * \return Number of bytes written, less than 0 on error
 */
typedef int (* mqtt_write_callback)(mqtt_client *client, const void *buffer, int len, int timeout_ms);
typedef void (* mqtt_event_callback)(mqtt_client *client, mqtt_event_data_t *event_data);

typedef struct mqtt_settings {
    mqtt_connect_callback connect_cb;
    mqtt_disconnect_callback disconnect_cb;

    mqtt_read_callback read_cb;
    mqtt_write_callback write_cb;

    mqtt_event_callback connected_cb;
    mqtt_event_callback disconnected_cb;

    mqtt_event_callback subscribe_cb;
    mqtt_event_callback unsubscribe_cb;
    mqtt_event_callback publish_cb;
    mqtt_event_callback data_cb;

    void *mpy_connected_cb;
    void *mpy_disconnected_cb;
    void *mpy_subscribed_cb;
    void *mpy_unsubscribed_cb;
    void *mpy_published_cb;
    void *mpy_data_cb;

    char host[CONFIG_MQTT_MAX_HOST_LEN];
    uint16_t port;
    char client_id[CONFIG_MQTT_MAX_CLIENT_LEN];
    char username[CONFIG_MQTT_MAX_USERNAME_LEN];
    char password[CONFIG_MQTT_MAX_PASSWORD_LEN];
    char lwt_topic[CONFIG_MQTT_MAX_LWT_TOPIC];
    char lwt_msg[CONFIG_MQTT_MAX_LWT_MSG];
    uint32_t lwt_msg_len;
    uint32_t lwt_qos;
    uint32_t lwt_retain;
    uint32_t clean_session;
    uint32_t keepalive;
    bool auto_reconnect;
    bool use_ssl;
    TaskHandle_t xMqttTask;
    TaskHandle_t xMqttSendingTask;
    uint32_t xMqttTask_stacksize;
    uint32_t xMqttSendingTask_stacksize;
} mqtt_settings;

typedef struct mqtt_event_data_t
{
  uint8_t type;
  const char* topic;
  const char* data;
  uint16_t topic_length;
  uint16_t data_length;
  uint32_t data_offset;
  uint32_t data_total_length;
} mqtt_event_data_t;

typedef struct mqtt_state_t
{
  uint16_t port;
  int auto_reconnect;
  mqtt_connect_info_t* connect_info;
  uint8_t* in_buffer;
  uint8_t* out_buffer;
  int in_buffer_length;
  int out_buffer_length;
  uint16_t message_length;
  uint16_t message_length_read;
  mqtt_message_t* outbound_message;
  mqtt_connection_t mqtt_connection;
  uint16_t pending_msg_id;
  int pending_msg_type;
  int sending_msg_type;
  int pending_publish_qos;
} mqtt_state_t;

typedef struct mqtt_client {
  int socket;
  SSL_CTX *ctx;
  SSL *ssl;
  mqtt_settings *settings;
  mqtt_state_t  mqtt_state;
  mqtt_connect_info_t connect_info;
  QueueHandle_t xSendingQueue;
  RINGBUF send_rb;
  uint32_t keepalive_tick;
  uint8_t status;
  uint8_t subs_flag;
  uint8_t unsubs_flag;
  uint8_t *msgbuf;
  uint8_t *topicbuf;
  bool terminate_mqtt;
  char *name;
} mqtt_client;

const char *MQTT_TAG;

int mqtt_start(mqtt_client *client);
void mqtt_stop(mqtt_client* client);
void mqtt_task(void *pvParameters);
void mqtt_subscribe(mqtt_client *client, const char *topic, uint8_t qos);
void mqtt_unsubscribe(mqtt_client *client, const char *topic);
int mqtt_publish(mqtt_client* client, const char *topic, const char *data, int len, int qos, int retain);
void mqtt_free(mqtt_client *client);

#endif

#endif
