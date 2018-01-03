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

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_MQTT

#include <stdio.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "ringbuf.h"
#include "mqtt.h"

#include "esp_wifi_types.h"
#include "tcpip_adapter.h"
#include "libs/libGSM.h"

const char *MQTT_TAG = "[Mqtt client]";
static char *subs_last_topic = NULL;
static char *unsubs_last_topic = NULL;

//----------------------------------------------------------------
static int resolve_dns(const char *host, struct sockaddr_in *ip) {
    struct hostent *he;
    struct in_addr **addr_list;
    he = gethostbyname(host);
    if (he == NULL) return 0;
    addr_list = (struct in_addr **)he->h_addr_list;
    if (addr_list[0] == NULL) return 0;
    ip->sin_family = AF_INET;
    memcpy(&ip->sin_addr, addr_list[0], sizeof(ip->sin_addr));
    return 1;
}

//-----------------------------------------
static void mqtt_queue(mqtt_client *client)
{
    int msg_len;
    while (rb_available(&client->send_rb) < client->mqtt_state.outbound_message->length) {
        xQueueReceive(client->xSendingQueue, &msg_len, 1000 / portTICK_RATE_MS);
        rb_read(&client->send_rb, client->mqtt_state.out_buffer, msg_len);
    }
    rb_write(&client->send_rb,
             client->mqtt_state.outbound_message->data,
             client->mqtt_state.outbound_message->length);
    xQueueSend(client->xSendingQueue, &client->mqtt_state.outbound_message->length, 0);
}

//---------------------------------------------
static bool client_connect(mqtt_client *client)
{
    struct sockaddr_in remote_ip;

    client->status = MQTT_STATUS_DISCONNECTED;
    while (1) {
        bzero(&remote_ip, sizeof(struct sockaddr_in));
        remote_ip.sin_family = AF_INET;
        remote_ip.sin_port = htons(client->settings->port);

        //if host is not ip address, resolve it
        if (inet_aton( client->settings->host, &(remote_ip.sin_addr)) == 0) {
            ESP_LOGI(MQTT_TAG, "Resolve dns for domain: %s", client->settings->host);

            if (!resolve_dns(client->settings->host, &remote_ip)) {
                ESP_LOGE(MQTT_TAG, "Resolve dns for domain: %s failed", client->settings->host);
                return false;
            }
        }

        if (client->settings->use_ssl) {
			client->ctx = NULL;
			client->ssl = NULL;

			client->ctx = SSL_CTX_new(TLSv1_2_client_method());
			if (!client->ctx) {
				ESP_LOGE(MQTT_TAG, "Failed to create SSL CTX");
				goto failed1;
			}
        }

        client->socket = socket(PF_INET, SOCK_STREAM, 0);
        if (client->socket == -1) {
            ESP_LOGE(MQTT_TAG, "Failed to create socket");
            goto failed2;
        }

        ESP_LOGI(MQTT_TAG, "Connecting to server %s:%d,%d", inet_ntoa((remote_ip.sin_addr)), client->settings->port, remote_ip.sin_port);

        if (connect(client->socket, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr)) != 00) {
            ESP_LOGE(MQTT_TAG, "Connect failed");
            goto failed3;
        }

        if (client->settings->use_ssl) {
			ESP_LOGI(MQTT_TAG, "Creating SSL object...");
			client->ssl = SSL_new(client->ctx);
			if (!client->ssl) {
				ESP_LOGE(MQTT_TAG, "Unable to create new SSL object");
				goto failed3;
			}

			if (!SSL_set_fd(client->ssl, client->socket)) {
				ESP_LOGE(MQTT_TAG, "SSL set_fd failed");
				goto failed3;
			}

			ESP_LOGI(MQTT_TAG, "Start SSL connect..");
			if (!SSL_connect(client->ssl)) {
				ESP_LOGE(MQTT_TAG, "SSL Connect FAILED");
				goto failed4;
			}
        }
        ESP_LOGI(MQTT_TAG, "Connected!");

        client->status = MQTT_STATUS_CONNECTED;
        return true;

        //failed5:
        //   SSL_shutdown(client->ssl);

failed4:  // SSL_CTX_new failed
        if (client->settings->use_ssl) {
			SSL_free(client->ssl);
			client->ssl = NULL;
        }

failed3:  // Connect failed
        close(client->socket);
        client->socket = -1;

failed2:  // Failed to create socket
        if (client->settings->use_ssl) {
        	SSL_CTX_free(client->ctx);
        }

failed1:
        if (client->settings->use_ssl) {
        	client->ctx = NULL;
        }
        return false;
     }
}


// Close client socket
// including SSL objects if enabled
//-----------------------------------
void closeclient(mqtt_client *client)
{
    if (client->socket != -1) {
		close(client->socket);
		client->socket = -1;
		ESP_LOGI(MQTT_TAG, "Closing client socket");
	}

    if (client->settings->use_ssl) {
		if (client->ssl != NULL) {
			SSL_shutdown(client->ssl);
			SSL_free(client->ssl);
			client->ssl = NULL;
		}

		if (client->ctx != NULL) {
			SSL_CTX_free(client->ctx);
			client->ctx = NULL;
		}
    }
    client->status = MQTT_STATUS_DISCONNECTED;
}

//-----------------------------------------------------------------------
int mqtt_read(mqtt_client *client, void *buffer, int len, int timeout_ms)
{
    int result;
    struct timeval tv;
    if (timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000;
        while (tv.tv_usec > 1000 * 1000) {
            tv.tv_usec -= 1000 * 1000;
            tv.tv_sec++;
        }
        setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    if (client->settings->use_ssl) result = SSL_read(client->ssl, buffer, len);
    else result = read(client->socket, buffer, len);

    if (timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    return result;
}

//------------------------------------------------------------------------------
int mqtt_write(mqtt_client *client, const void *buffer, int len, int timeout_ms)
{
    int result;
    struct timeval tv;
    if (timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000;
        while (tv.tv_usec > 1000 * 1000) {
            tv.tv_usec -= 1000 * 1000;
            tv.tv_sec++;
        }
        setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    if (client->settings->use_ssl) result = SSL_write(client->ssl, buffer, len);
    else result = write(client->socket, buffer, len);

    if (timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    return result;
}

/*
 * mqtt_connect
 * input - client
 * return 1: success, 0: fail
 */
//-------------------------------------------
static bool mqtt_connect(mqtt_client *client)
{
    int write_len, read_len, connect_rsp_code;

    mqtt_msg_init(&client->mqtt_state.mqtt_connection, client->mqtt_state.out_buffer, client->mqtt_state.out_buffer_length);
    client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection, client->mqtt_state.connect_info);
    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);

    ESP_LOGI(MQTT_TAG, "Sending MQTT CONNECT message, type: %d, id: %04X", client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);

    write_len = client->settings->write_cb(client, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length, 0);
    if(write_len < 0) {
        ESP_LOGE(MQTT_TAG, "Writing failed: %d", errno);
        return false;
    }

    ESP_LOGI(MQTT_TAG, "Reading MQTT CONNECT response message");

    read_len = client->settings->read_cb(client, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE, 10 * 1000);

    if (read_len < 0) {
        ESP_LOGE(MQTT_TAG, "Error network response");
        return false;
    }
    if (mqtt_get_type(client->mqtt_state.in_buffer) != MQTT_MSG_TYPE_CONNACK) {
        ESP_LOGE(MQTT_TAG, "Invalid MSG_TYPE response: %d, read_len: %d", mqtt_get_type(client->mqtt_state.in_buffer), read_len);
        return false;
    }
    connect_rsp_code = mqtt_get_connect_return_code(client->mqtt_state.in_buffer);
    switch (connect_rsp_code) {
        case CONNECTION_ACCEPTED:
            ESP_LOGI(MQTT_TAG, "Connected");
            return true;
        case CONNECTION_REFUSE_PROTOCOL:
            ESP_LOGW(MQTT_TAG, "Connection refused, bad protocol");
            return false;
        case CONNECTION_REFUSE_SERVER_UNAVAILABLE:
            ESP_LOGW(MQTT_TAG, "Connection refused, server unavailable");
            return false;
        case CONNECTION_REFUSE_BAD_USERNAME:
            ESP_LOGW(MQTT_TAG, "Connection refused, bad username or password");
            return false;
        case CONNECTION_REFUSE_NOT_AUTHORIZED:
            ESP_LOGW(MQTT_TAG, "Connection refused, not authorized");
            return false;
        default:
            ESP_LOGW(MQTT_TAG, "Connection refused, Unknown reason");
            return false;
    }
    return false;
}

//========================================
void mqtt_sending_task(void *pvParameters)
{
    mqtt_client *client = (mqtt_client *)pvParameters;
    uint32_t msg_len;
    int send_len, sent_len = 0;
    bool connected = true;
    ESP_LOGI(MQTT_TAG, "Sending task started");

    while (connected) {
    	if (client->terminate_mqtt) {
            ESP_LOGI(MQTT_TAG, "Terminate, sending task exit.");
    	    client->status = MQTT_STATUS_STOPPING;
    		break;
    	}
        if (xQueueReceive(client->xSendingQueue, &msg_len, 1000 / portTICK_RATE_MS)) {
            //queue available
            while (msg_len > 0) {
                send_len = msg_len;
                if (send_len > CONFIG_MQTT_BUFFER_SIZE_BYTE) send_len = CONFIG_MQTT_BUFFER_SIZE_BYTE;
                ESP_LOGD(MQTT_TAG, "Sending %d bytes", send_len);

                rb_read(&client->send_rb, client->mqtt_state.out_buffer, send_len);
                client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.out_buffer);
                client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.out_buffer, send_len);
                send_len = client->settings->write_cb(client, client->mqtt_state.out_buffer, send_len, 5 * 1000);
                if (send_len <= 0) {
                    ESP_LOGE(MQTT_TAG, "Write error: %d", errno);
                    connected = false;
                    break;
                }

                //TODO: Check sending type, to callback publish message
                msg_len -= send_len;
                sent_len += send_len;
            }
            //invalidate keepalive timer
            client->keepalive_tick = client->settings->keepalive / 2;
        	if (client->mqtt_state.sending_msg_type == MQTT_SENDING_TYPE_PUBLISH) {
        		client->mqtt_state.sending_msg_type = MQTT_SENDING_TYPE_NONE;
                ESP_LOGI(MQTT_TAG, "Published %d bytes", sent_len);
                if (client->settings->publish_cb) {
                    client->settings->publish_cb(client, (void *)"Sent");
                }
        	}
        }
        else {
            if (client->keepalive_tick > 0) client->keepalive_tick --;
            else {
                client->keepalive_tick = client->settings->keepalive / 2;
                client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
                client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
                client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data,
                                                    client->mqtt_state.outbound_message->length);
                ESP_LOGD(MQTT_TAG, "Sending ping request");
                send_len = client->settings->write_cb(client,
                      client->mqtt_state.outbound_message->data,
                      client->mqtt_state.outbound_message->length, 0);
                if(send_len <= 0) {
					ESP_LOGE(MQTT_TAG, "Write error: %d", errno);
                    connected = false;
					break;
				}
            }
        }
    }
    closeclient(client);
    client->settings->xMqttSendingTask = NULL;
    vTaskDelete(NULL);
}

//---------------------------------------------------------------------
void deliver_publish(mqtt_client *client, uint8_t *message, int length)
{
    mqtt_event_data_t event_data;
    int len_read, total_mqtt_len = 0, mqtt_len = 0, mqtt_offset = 0;
    uint8_t do_cb = (client->settings->data_cb != NULL);
    do
    {
        if(total_mqtt_len == 0){
            event_data.topic_length = length;
            event_data.topic = mqtt_get_publish_topic(message, &event_data.topic_length);
            event_data.data_length = length;
            event_data.data = mqtt_get_publish_data(message, &event_data.data_length);
            total_mqtt_len = client->mqtt_state.message_length - client->mqtt_state.message_length_read + event_data.data_length;
            if (total_mqtt_len > CONFIG_MQTT_MAX_PAYLOAD_SIZE) event_data.data_total_length = CONFIG_MQTT_MAX_PAYLOAD_SIZE;
            else event_data.data_total_length = total_mqtt_len;
            mqtt_len = event_data.data_length;
        } else {
            mqtt_len = len_read;
            event_data.data = (const char*)client->mqtt_state.in_buffer;
        }

        event_data.data_offset = mqtt_offset;
        event_data.data_length = mqtt_len;

        ESP_LOGD(MQTT_TAG, "Data received: %d/%d bytes ", mqtt_len, total_mqtt_len);
        if (do_cb) {
            if ((mqtt_offset+mqtt_len) > CONFIG_MQTT_MAX_PAYLOAD_SIZE) {
            	event_data.data_length = CONFIG_MQTT_MAX_PAYLOAD_SIZE - mqtt_offset;
            	do_cb = 0;
            }
            client->settings->data_cb(client, &event_data);
        }
        mqtt_offset += mqtt_len;
        if (client->mqtt_state.message_length_read >= client->mqtt_state.message_length)
            break;

        len_read = client->settings->read_cb(client, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE, 0);
        if(len_read < 0) {
            ESP_LOGE(MQTT_TAG, "Read error: %d", errno);
            break;
        }
        client->mqtt_state.message_length_read += len_read;
    } while (1);

}

//---------------------------------------------------
void mqtt_start_receive_schedule(mqtt_client *client)
{
    int read_len;
    uint8_t msg_type;
    uint8_t msg_qos;
    uint16_t msg_id;

    while (1) {
    	if (client->terminate_mqtt) {
            ESP_LOGI(MQTT_TAG, "Terminate, receive schedule exit.");
    	    client->status = MQTT_STATUS_STOPPING;
    		break;
    	}
    	if (client->settings->xMqttSendingTask == NULL) break;

        read_len = client->settings->read_cb(client, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE, 0);

        ESP_LOGD(MQTT_TAG, "Read length %d", read_len);
        if (read_len <= 0) {
        	if (client->terminate_mqtt) {
                ESP_LOGI(MQTT_TAG, "Terminate, receive schedule exit.");
        	    client->status = MQTT_STATUS_STOPPING;
        	}
        	else {
        		ESP_LOGE(MQTT_TAG, "Socket error (%d), exit Receive schedule", errno);
        	}
            break;
        }

        msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
        msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
        msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
        // ESP_LOGI(MQTT_TAG, "msg_type %d, msg_id: %d, pending_id: %d", msg_type, msg_id, client->mqtt_state.pending_msg_type);
        switch (msg_type)
        {
            case MQTT_MSG_TYPE_SUBACK:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_SUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id) {
                    ESP_LOGI(MQTT_TAG, "Subscribe successful");
                    client->subs_flag = 1;
                    if (client->settings->subscribe_cb) {
                        client->settings->subscribe_cb(client, (void *)subs_last_topic);
                    }
                }
                break;
            case MQTT_MSG_TYPE_UNSUBACK:
                //if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_UNSUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id) {
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_UNSUBSCRIBE) {
                    ESP_LOGI(MQTT_TAG, "UnSubscribe successful");
                    client->unsubs_flag = 1;
                    client->subs_flag = 1;
                    if (client->settings->unsubscribe_cb) {
                        client->settings->unsubscribe_cb(client, (void *)unsubs_last_topic);
                    }
                }
                break;
            case MQTT_MSG_TYPE_PUBLISH:
                if (msg_qos == 1)
                    client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
                else if (msg_qos == 2)
                    client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);

                if (msg_qos == 1 || msg_qos == 2) {
                    ESP_LOGI(MQTT_TAG, "Queue response QoS: %d", msg_qos);
                    mqtt_queue(client);
                    // if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                    //     ESP_LOGI(MQTT_TAG, "MQTT: Queue full");
                    // }
                }
                client->mqtt_state.message_length_read = read_len;
                client->mqtt_state.message_length = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
                ESP_LOGI(MQTT_TAG, "deliver_publish");

                deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
                break;
            case MQTT_MSG_TYPE_PUBACK:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
                    ESP_LOGI(MQTT_TAG, "received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish");
                    if (client->settings->publish_cb) {
                        client->settings->publish_cb(client, (void *)"QoS1 acknowledged");
                    }
                }
                break;
            case MQTT_MSG_TYPE_PUBREC:
                client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
                mqtt_queue(client);
                break;
            case MQTT_MSG_TYPE_PUBREL:
                client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
                mqtt_queue(client);
                break;
            case MQTT_MSG_TYPE_PUBCOMP:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBREL && client->mqtt_state.pending_msg_id == msg_id) {
                    ESP_LOGI(MQTT_TAG, "Receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish");
                    if (client->settings->publish_cb) {
                        client->settings->publish_cb(client, (void *)"QoS2 acknowledged");
                    }
                }
                break;
            case MQTT_MSG_TYPE_PINGREQ:
            	client->mqtt_state.sending_msg_type = MQTT_SENDING_TYPE_PING;
                client->mqtt_state.outbound_message = mqtt_msg_pingresp(&client->mqtt_state.mqtt_connection);
                mqtt_queue(client);
                break;
            case MQTT_MSG_TYPE_PINGRESP:
                ESP_LOGD(MQTT_TAG, "MQTT_MSG_TYPE_PINGRESP");
                // Ignore
                break;
        }
    }
}

//---------------------------------
void mqtt_free(mqtt_client *client)
{
	if (client == NULL) return;

	vQueueDelete(client->xSendingQueue);

    free(client->mqtt_state.in_buffer);
    free(client->mqtt_state.out_buffer);
    free(client->send_rb.p_o);

    ESP_LOGI(MQTT_TAG, "Client freed");
}

//================================
void mqtt_task(void *pvParameters)
{
    ESP_LOGI(MQTT_TAG, "Starting Mqtt task");

    mqtt_client *client = (mqtt_client *)pvParameters;

    while (1) {
    	if (client->terminate_mqtt) {
    	    client->status = MQTT_STATUS_STOPPING;
    		break;
    	}

        if (client->settings->connect_cb(client) == false) {
            ESP_LOGE(MQTT_TAG, "Connection to server %s:%d failed!", client->settings->host, client->settings->port);
    	    client->status = MQTT_STATUS_STOPPING;
    		break;
        }

        ESP_LOGI(MQTT_TAG, "Connected to server %s:%d", client->settings->host, client->settings->port);
        if (!mqtt_connect(client)) {
            client->settings->disconnect_cb(client);

            if (client->settings->disconnected_cb) {
				client->settings->disconnected_cb(client, NULL);
			}

            if (!client->settings->auto_reconnect) {
        	    client->status = MQTT_STATUS_STOPPING;
				break;
			}
            else continue;
        }

        ESP_LOGI(MQTT_TAG, "Connected to MQTT broker, creating sending thread before calling connected callback");
        xTaskCreate(&mqtt_sending_task, "mqtt_sending_task", client->settings->xMqttSendingTask_stacksize, client, CONFIG_MQTT_PRIORITY + 1, &(client->settings->xMqttSendingTask));
        if (client->settings->xMqttSendingTask == NULL) break;
        if (client->settings->connected_cb) {
            client->settings->connected_cb(client, NULL);
        }

        ESP_LOGI(MQTT_TAG, "mqtt_start_receive_schedule");
        mqtt_start_receive_schedule(client);

        client->settings->disconnect_cb(client);
        if (client->settings->disconnected_cb) {
        	client->settings->disconnected_cb(client, NULL);
		}

        if (client->settings->xMqttSendingTask != NULL) {
        	vTaskDelete(client->settings->xMqttSendingTask);
        }
        if (!client->settings->auto_reconnect) {
    	    client->status = MQTT_STATUS_STOPPING;
			break;
		}
        vTaskDelay(1000 / portTICK_RATE_MS);

    }

    mqtt_free(client);
    client->settings->xMqttTask = NULL;
    client->status = MQTT_STATUS_STOPPED;
    vTaskDelete(NULL);
}

// =================================
int mqtt_start(mqtt_client *client)
{
	// ==== Check for Internet connection first ====
    tcpip_adapter_ip_info_t info;
    tcpip_adapter_get_ip_info(WIFI_IF_STA, &info);
    if (info.ip.addr == 0) {
		#ifdef CONFIG_MICROPY_USE_GSM
    	if (ppposStatus() != GSM_STATE_CONNECTED) {
    		return -9;
    	}
		#else
		return -9;
		#endif
    }
	// =============================================

    client->terminate_mqtt = false;

    uint8_t *rb_buf;
    if (client->settings->xMqttTask != NULL) return -1;

    client->status = MQTT_STATUS_DISCONNECTED;
    client->settings->xMqttSendingTask = NULL;
    client->settings->xMqttSendingTask_stacksize = 2048;
    client->settings->xMqttTask_stacksize = 2048;

    client->connect_info.client_id = client->settings->client_id;
    client->connect_info.username = client->settings->username;
    client->connect_info.password = client->settings->password;
    client->connect_info.will_topic = client->settings->lwt_topic;
    client->connect_info.will_message = client->settings->lwt_msg;
    client->connect_info.will_qos = client->settings->lwt_qos;
    client->connect_info.will_retain = client->settings->lwt_retain;
    client->connect_info.will_length = client->settings->lwt_msg_len;

    client->keepalive_tick = client->settings->keepalive / 2;

    client->connect_info.keepalive = client->settings->keepalive;
    client->connect_info.clean_session = client->settings->clean_session;

    client->mqtt_state.in_buffer = (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.in_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.out_buffer =  (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.out_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.connect_info = &client->connect_info;

    client->socket = -1;

    if (!client->settings->connect_cb)
        client->settings->connect_cb = client_connect;
    if (!client->settings->disconnect_cb)
        client->settings->disconnect_cb = closeclient;
    if (!client->settings->read_cb)
        client->settings->read_cb = mqtt_read;
    if (!client->settings->write_cb)
        client->settings->write_cb = mqtt_write;

    client->ctx = NULL;
    client->ssl = NULL;
    if (client->settings->use_ssl) client->settings->xMqttTask_stacksize = 10240; // Need more stack to handle SSL handshake

    /* Create a queue capable of containing 64 unsigned long values. */
    client->xSendingQueue = xQueueCreate(64, sizeof( uint32_t ));
    if (client->xSendingQueue == 0) return -3;

    rb_buf = (uint8_t*) malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE * 4);

    if (rb_buf == NULL) {
        ESP_LOGE(MQTT_TAG, "Error allocating ring buffer");
        return -2;
    }

    rb_init(&client->send_rb, rb_buf, CONFIG_MQTT_BUFFER_SIZE_BYTE * 4, 1);

    mqtt_msg_init(&client->mqtt_state.mqtt_connection,
                  client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);

    xTaskCreate(&mqtt_task, "mqtt_task", client->settings->xMqttTask_stacksize, client, CONFIG_MQTT_PRIORITY, &client->settings->xMqttTask);
    if (client->settings->xMqttTask == NULL) return -4;

    return 0;
}

//----------------------------------------------------------------------
void mqtt_subscribe(mqtt_client *client, const char *topic, uint8_t qos)
{
	if (subs_last_topic) free(subs_last_topic);
	subs_last_topic = malloc(strlen(topic)+1);
	if (subs_last_topic) strcpy(subs_last_topic, topic);

	client->subs_flag = 0;
	client->mqtt_state.sending_msg_type = MQTT_SENDING_TYPE_SUBSCRIBE;
    client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                          topic, qos,
                                          &client->mqtt_state.pending_msg_id);
    ESP_LOGI(MQTT_TAG, "Queue subscribe, topic \"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);
    mqtt_queue(client);
}

//-----------------------------------------------------------
void mqtt_unsubscribe(mqtt_client *client, const char *topic)
{
	if (unsubs_last_topic) free(unsubs_last_topic);
	unsubs_last_topic = malloc(strlen(topic)+1);
	if (unsubs_last_topic) strcpy(unsubs_last_topic, topic);

    client->unsubs_flag = 0;
	client->mqtt_state.sending_msg_type = MQTT_SENDING_TYPE_UNSUBSCRIBE;
	client->mqtt_state.outbound_message = mqtt_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
	                                          topic,
	                                          &client->mqtt_state.pending_msg_id);
	ESP_LOGI(MQTT_TAG, "Queue unsubscribe, topic \"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);
	mqtt_queue(client);
}

//------------------------------------------------------------------------------------------------------
int mqtt_publish(mqtt_client* client, const char *topic, const char *data, int len, int qos, int retain)
{
    client->mqtt_state.outbound_message = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
                                          topic, data, len,
                                          qos, retain,
                                          &client->mqtt_state.pending_msg_id);
    if (client->mqtt_state.outbound_message->length == 0) return -1;

	client->mqtt_state.sending_msg_type = MQTT_SENDING_TYPE_PUBLISH;
    mqtt_queue(client);
    ESP_LOGI(MQTT_TAG, "Queuing publish, length: %d, queue size(%d/%d)",
              client->mqtt_state.outbound_message->length,
              client->send_rb.fill_cnt,
              client->send_rb.size);
    return 0;
}

//---------------------------------
void mqtt_stop(mqtt_client* client)
{
	client->terminate_mqtt = true;
    client->status = MQTT_STATUS_STOPPING;
}

#endif
