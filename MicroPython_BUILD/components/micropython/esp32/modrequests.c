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

#ifdef CONFIG_MICROPY_USE_REQUESTS

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "mbedtls/base64.h"

#include "esp_http_client.h"
#include "transport.h"
#include "esp_wifi_types.h"
#include "tcpip_adapter.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "extmod/vfs_native.h"
#include "modnetwork.h"

#define MAX_HTTP_RECV_BUFFER 512
static const char *TAG = "[REQUESTS]";

static char *rqheader = NULL;
static char *rqbody = NULL;
static FILE* rqbody_file = NULL;
static int rqheader_len = 0;
static int rqheader_ptr = 0;
static int rqbody_len = 0;
static int rqbody_ptr = 0;
static bool rqbody_ok = true;
static bool rq_debug = false;
static bool rq_base64 = false;
static char *cert_pem = NULL;


/*
 * You can get the Root cert for the server using openssl

   The PEM file can be  extracted from the output of this command:
   openssl s_client -showcerts -connect <the_server>:443

   The CA root cert is the last cert given in the chain of certs.
*/

//----------------------------------------------------------------
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            if (rq_debug) {
                ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
                ESP_LOGD(TAG, "  KEY: [%s]", evt->header_key);
                ESP_LOGD(TAG, "VALUE: [%s]", evt->header_value);
            }
            break;
        case HTTP_EVENT_ON_HEADER:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            if (rqheader == NULL) {
                rqheader = malloc(256);
                if (rqheader) {
                    rqheader_len = 256;
                    rqheader_ptr = 0;
                    rqheader[0] = '\0';
                }
            }
            if (rqheader) {
                bool f = true;
                int len = strlen(evt->header_key) + strlen(evt->header_value) + rqheader_ptr + 5;
                if (len > rqheader_len) {
                    char *tmphdr = realloc(rqheader, len + 128);
                    if (tmphdr) {
                        rqheader = tmphdr;
                        rqheader_len = len + 128;
                    }
                    else f = false;
                }
                if (f) {
                    strcat(rqheader, evt->header_key);
                    strcat(rqheader, ": ");
                    strcat(rqheader, evt->header_value);
                    strcat(rqheader, "\r\n");
                    rqheader_ptr = strlen(rqheader);
                }
            }
            break;
        case HTTP_EVENT_ON_DATA:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d, rqptr=%d [%d]", evt->data_len, rqbody_ptr, rqbody_len);
            if (rqbody_ok) {
                if (rqbody_file) {
                    int nwrite = fwrite(evt->data, 1, evt->data_len, rqbody_file);
                    if (nwrite <= 0) {
                        rqbody_ok = false;
                        ESP_LOGE(TAG, "Download: Error writing to file %d", nwrite);
                    }
                }
                else {
                    if (rqbody == NULL) {
                        rqbody = malloc(4096);
                        if (rqbody) {
                            rqbody_len = 4096;
                            rqbody_ptr = 0;
                        }
                    }
                    if (rqbody) {
                        int len = evt->data_len + rqbody_ptr;
                        if (len > rqbody_len) {
                            char *tmpbody = realloc(rqbody, len + 512);
                            if (tmpbody) {
                                rqbody = tmpbody;
                                rqbody_len = len + 512;
                            }
                            else {
                                rqbody_ok = false;
                                ESP_LOGE(TAG, "Error reallocating body buffer");
                            }
                        }

                        if (rqbody_ok) {
                            memcpy(rqbody + rqbody_ptr, evt->data, evt->data_len);
                            rqbody_ptr += evt->data_len;
                        }
                    }
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

/*
//---------------------------
static void http_auth_basic()
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/basic-auth/user/passwd",
        .event_handler = _http_event_handler,
        .auth_type = HTTP_AUTH_TYPE_BASIC,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Basic Auth Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

//------------------------------------
static void http_auth_basic_redirect()
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/basic-auth/user/passwd",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Basic Auth redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

//----------------------------
static void http_auth_digest()
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/digest-auth/auth/user/passwd/MD5/never",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Digest Auth Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

//-----------------
static void https()
{
    esp_http_client_config_t config = {
        .url = "https://www.howsmyssl.com",
        .event_handler = _http_event_handler,
        //.cert_pem = howsmyssl_com_root_cert_pem_start,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_relative_redirect()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/relative-redirect/3",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Relative path redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_absolute_redirect()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/absolute-redirect/3",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Absolute path redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_redirect_to_https()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/redirect-to?url=https%3A%2F%2Fwww.howsmyssl.com",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP redirect to HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}


static void http_download_chunk()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/stream-bytes/8912",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP chunk encoding Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_perform_as_stream_reader()
{
    char *buffer = malloc(MAX_HTTP_RECV_BUFFER);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Cannot malloc http receive buffer");
        return;
    }
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(buffer);
        return;
    }
    int content_length =  esp_http_client_fetch_headers(client);
    int total_read_len = 0, read_len;
    if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER) {
        read_len = esp_http_client_read(client, buffer, content_length);
        if (read_len <= 0) {
            ESP_LOGE(TAG, "Error read data");
        }
        buffer[read_len] = 0;
        ESP_LOGD(TAG, "read_len = %d", read_len);
    }
    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buffer);
}
*/

//-----------------------------------------------
static char *url_post_fields(mp_obj_dict_t *dict)
{
    char *params = calloc(256, sizeof(char));
    if (params == NULL) return 0;

    int nparam = 0;
    const char *key;
    const char *value;
    char sval[64];
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

        value = NULL;
        key = mp_obj_str_get_str(next->key);
        if (MP_OBJ_IS_STR(next->value)) {
            value = mp_obj_str_get_str(next->value);
            for (int i=0; i<strlen(value); i++) {
                if ((value[i] < 0x20) || (value[i] > 0x7F)) {
                    value = NULL;
                    break;
                }
            }
        }
        else if (MP_OBJ_IS_INT(next->value)) {
            int ival = mp_obj_get_int(next->value);
            sprintf(sval,"%d", ival);
            value = sval;
        }
        else if (MP_OBJ_IS_TYPE(next->value, &mp_type_float)) {
            double fval = mp_obj_get_float(next->value);
            sprintf(sval,"%f", fval);
            value = sval;
        }
        if ((value) && ((strlen(params) + strlen(key) + strlen(value) + 2) < 256)) {
            if (strlen(params) > 0) strcat(params, "&");
            strcat(params, key);
            strcat(params, "=");
            strcat(params, value);
            nparam++;
        }
    }
    if (nparam == 0) {
        free(params);
        params = NULL;
    }
    return params;
}

//-------------------------------------------------------------------------------------------------------------------------------
static int post_field(esp_http_client_handle_t client, char *bndry, char *key, char *cont_type, char *value, int vlen, bool send)
{
    int len = vlen;
    int res;
    char buff[256];
    sprintf(buff, "--%s\r\nContent-Disposition: form-data; name=%s\r\n%s\r\n\r\n", bndry, key, cont_type);
    len += strlen(buff);
    if (send) {
        res = esp_http_client_write(client, buff, strlen(buff));
        if (rq_debug) printf("%s", buff);
        if (res <= 0) return res;
        res = esp_http_client_write(client, value, vlen);
        if (rq_debug) printf("%s", value);
        if (res <= 0) return res;
    }

    sprintf(buff, "\r\n--%s\r\n\r\n", bndry);
    len += strlen(buff);
    if (send) {
        res = esp_http_client_write(client, buff, strlen(buff));
        if (rq_debug) printf("%s", buff);
        if (res <= 0) return res;
    }
    return len;
}

//-----------------------------------------------------------------------------------------------------
static int handle_file(esp_http_client_handle_t client, char *bndry, char *key, char *fname, bool send)
{
    int flen = 0;
    if (strlen(fname) < 128) {
        FILE* file = NULL;
        char fullname[128] = {'\0'};
        int res = physicalPath(fname, fullname);
        if ((res == 0) && (strlen(fullname) > 0)) {
            file = fopen(fullname, "rb");
            if (file == NULL) return flen;

            char *buff = malloc(1024);
            if (buff == NULL) return flen;

            if (key) {
                sprintf(buff, "--%s\r\nContent-Disposition: form-data; name=\"file_%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n", bndry, key, fname);
                flen += strlen(buff);
                if (send) {
                    res = esp_http_client_write(client, buff, strlen(buff));
                    if (rq_debug) printf("%s", buff);
                    if (res <= 0) {
                        free(buff);
                        return -1;
                    }
                }
            }
            res = 1;
            while (res > 0) {
                res = fread(buff, 1, 1023, file);
                if (send) {
                    if (res > 0) buff[res] = 0;
                    if ((rq_debug) && (res > 0)) printf("%s", buff);
                    res = esp_http_client_write(client, buff, res);
                }
                flen += res;
            }
            fclose(file);
            if (key) {
                sprintf(buff, "\r\n--%s\r\n\r\n", bndry);
                flen += strlen(buff);
                if (send) {
                    res = esp_http_client_write(client, buff, strlen(buff));
                    if (rq_debug) printf("%s", buff);
                    if (res <= 0) {
                        free(buff);
                        return -1;
                    }
                }
            }
            free(buff);
        }
    }
    return flen;
}

// Parse MicroPython dictionary, calculate the content length
// or send the body to the server
//------------------------------------------------------------------------------------------------------------
static int multipart_post_fields(mp_obj_dict_t *dict, char *bndry, esp_http_client_handle_t client, bool send)
{
    int data_len = 0;
    int res;
    int nparam = 0;
    char *key;
    char *value;
    int vlen;
    bool value_free;
    char sval[64];
    char cont_type[64];

    sprintf(cont_type, "%s", "Content-Type: text/plain");

    // Parse dictionary
    size_t max = dict->map.alloc;
    mp_map_t *map = &dict->map;
    mp_map_elem_t *next;
    size_t cur = 0;
    if ((rq_debug) && (send)) printf(">>> SEND FIELDS [\n");
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

        value = NULL;
        value_free = false;
        key = (char *)mp_obj_str_get_str(next->key);

        if (MP_OBJ_IS_STR(next->value)) {
            // String, it can be string to send or file name
            value = (char *)mp_obj_str_get_str(next->value);
            vlen = strlen(value);
            res = handle_file(client, bndry, key, value, send);
            if (res == 0) {
                for (int i=0; i<strlen(value); i++) {
                    if ((value[i] < 0x20) || (value[i] > 0x7F)) {
                        value = NULL;
                        break;
                    }
                }
            }
            else {
                data_len += res;
                nparam++;
                value = NULL;
            }
        }
        else if (MP_OBJ_IS_INT(next->value)) {
            int ival = mp_obj_get_int(next->value);
            sprintf(sval,"%d", ival);
            value = sval;
            vlen = strlen(sval);
        }
        else if (MP_OBJ_IS_TYPE(next->value, &mp_type_float)) {
            double fval = mp_obj_get_float(next->value);
            sprintf(sval,"%f", fval);
            value = sval;
            vlen = strlen(sval);
        }
        else {
            mp_obj_type_t *type = mp_obj_get_type(next->value);
            mp_buffer_info_t value_buff;
            if (type->buffer_p.get_buffer != NULL) {
                int ret = type->buffer_p.get_buffer(next->value, &value_buff, MP_BUFFER_READ);
                if ((ret == 0) && (value_buff.len > 0)) {
                    if (rq_base64) {
                        // Send base64 encoded
                        value = malloc(value_buff.len * 4 / 3 + 64);
                        if (value) {
                            ret = mbedtls_base64_encode((unsigned char *)value, value_buff.len * 4 / 3 + 64, (size_t *)&vlen, (const unsigned char *)value_buff.buf, value_buff.len);
                            if (ret == 0) {
                                value[vlen] = '\0';
                                sprintf(cont_type, "%s", "Content-Type: binary\r\nContent-Encoding: base64");
                                value_free = true;
                            }
                            else {
                                free((void *)value);
                                value = NULL;
                            }
                        }
                    }
                    else {
                        value = value_buff.buf;
                        vlen = value_buff.len;
                    }
                }
            }
        }
        if (value) {
            res = post_field(client, bndry, key, cont_type, value, vlen, send);
            if (value_free) free((void *)value);
            if (res <= 0) return res;
            data_len += res;
            nparam++;
        }
    }
    if (rq_debug) {
        if (send) {
            printf("] LENGTH=%d\n", data_len);
        }
        else printf(">>> CONTENT LENGTH = %d\n", data_len);
    }
    return data_len;
}

//--------------------------------------------------------------------------------------------------
static mp_obj_t request(int method, bool multipart, mp_obj_t post_data_in, char * url, char *tofile)
{
    int status;
    char fullname[128] = {'\0'};
    char err_msg[128] = {'\0'};
    esp_err_t err;
    bool perform_handled = false;
    bool free_post_data = false;

    // Disable logging
    if (!rq_debug) {
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
        esp_log_level_set("TRANSPORT", ESP_LOG_WARN);
        esp_log_level_set("TRANS_SSL", ESP_LOG_WARN);
    }

    // Check if the response is redirected to file
    rqbody_file = NULL;
    if (tofile) {
        // GET to file
        int res = physicalPath(tofile, fullname);
        if ((res != 0) || (strlen(fullname) == 0)) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
        }
        rqbody_file = fopen(fullname, "wb");
        if (rqbody_file == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error opening file"));
        }
    }

    char* post_data = NULL;
    char bndry[32];

    esp_http_client_config_t config = {0};
    config.url = url;
    config.event_handler = _http_event_handler;
    config.buffer_size = 1024;

    // Initialize the http_client and set the method
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        if (rqbody_file) fclose(rqbody_file);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error initializing http client"));
    }
    esp_http_client_set_method(client, method);

    if (rqheader) free(rqheader);
    if (rqbody) free(rqbody);
    rqheader = NULL;
    rqheader_len = 0;
    rqbody = NULL;
    rqbody_len = 0;
    rqbody_ptr = 0;
    rqbody_ok = true;

    if (method == HTTP_METHOD_POST) {
        mp_obj_dict_t *dict;
        if (!multipart) {
            if (MP_OBJ_IS_TYPE(post_data_in, &mp_type_dict)) {
                dict = MP_OBJ_TO_PTR(post_data_in);
                post_data = url_post_fields(dict);
                err = esp_http_client_set_post_field(client, post_data, strlen(post_data));
                if (err != ESP_OK) {
                    if (rqbody_file) fclose(rqbody_file);
                    free(post_data);
                    nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error setting post fields"));
                }
                free_post_data = true;
            }
            else if (MP_OBJ_IS_STR(post_data_in)) {
                post_data = (char *)mp_obj_str_get_str(post_data_in);
                err = esp_http_client_set_post_field(client, post_data, strlen(post_data));
                if (err != ESP_OK) {
                    if (rqbody_file) fclose(rqbody_file);
                    nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error setting post fields"));
                }
            }
            else {
                if (rqbody_file) fclose(rqbody_file);
                nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Expected Dict or String type argument"));
            }
        }
        else {
            // === multipart POST ===
            if (MP_OBJ_IS_TYPE(post_data_in, &mp_type_dict)) {
                dict = MP_OBJ_TO_PTR(post_data_in);
            }
            else {
                if (rqbody_file) fclose(rqbody_file);
                nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Expected Dict type argument"));
            }

            // Prepare multipart boundary
            memset(bndry,0x00,20);
            int randn = rand();
            sprintf(bndry, "_____%d_____", randn);
            // Get body length
            int cont_len = multipart_post_fields(dict, bndry, client, false);
            if (cont_len <= 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Nothing to send"));
            }
            char temp_buf[128];
            sprintf(temp_buf, "multipart/form-data; boundary=%s", bndry);
            esp_http_client_set_header(client, "Content-Type", temp_buf);

            // Perform actions
            MP_THREAD_GIL_EXIT();
            err = ESP_OK;
            do {
                if ((err = esp_http_client_open(client, cont_len)) != ESP_OK) {
                    sprintf(err_msg, "Http client error: open");
                    break;
                }

                // Send content
                cont_len = multipart_post_fields(dict, bndry, client, true);

                // Check response
                if ((esp_http_client_perform_response(client)) != ESP_OK) {
                    sprintf(err_msg, "Http client error: response");
                    break;
                }
            } while (esp_http_client_process_again(client));
            esp_http_client_cleanup(client);
            MP_THREAD_GIL_ENTER();
            perform_handled = true;
        }
    }
    else if ((method == HTTP_METHOD_PUT) || (method == HTTP_METHOD_PATCH)) {
        // String, it can be string to send or file name
        if (MP_OBJ_IS_STR(post_data_in)) {
            post_data = (char *)mp_obj_str_get_str(post_data_in);
            int cont_len = handle_file(client, NULL, NULL, post_data, false);
            if (cont_len > 0) {
                // Perform actions
                MP_THREAD_GIL_EXIT();
                err = ESP_OK;
                do {
                    if ((err = esp_http_client_open(client, cont_len)) != ESP_OK) {
                        sprintf(err_msg, "Http client error: open");
                        break;
                    }

                    // Send content
                    cont_len = handle_file(client, NULL, NULL, post_data, true);

                    // Check response
                    if ((esp_http_client_perform_response(client)) != ESP_OK) {
                        sprintf(err_msg, "Http client error: response");
                        break;
                    }
                } while (esp_http_client_process_again(client));
                esp_http_client_cleanup(client);
                MP_THREAD_GIL_ENTER();
                perform_handled = true;
            }
            else {
                err = esp_http_client_set_post_field(client, post_data, strlen(post_data));
                if (err != ESP_OK) {
                    if (rqbody_file) fclose(rqbody_file);
                    nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error setting post fields"));
                }
            }
        }
        else {
            if (rqbody_file) fclose(rqbody_file);
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Expected String type argument"));
        }
    }

    if (!perform_handled) {
        MP_THREAD_GIL_EXIT();
        err = esp_http_client_perform(client);
        esp_http_client_cleanup(client);
        if ((free_post_data) && (post_data)) free(post_data);
        MP_THREAD_GIL_ENTER();
    }

    if (err != ESP_OK) {
        if (rqbody_file) fclose(rqbody_file);
        if (rqheader) free(rqheader);
        if (rqbody) free(rqbody);
        rqbody_file = NULL;
        rqheader = NULL;
        rqbody = NULL;
        ESP_LOGE(TAG, "HTTP Request failed: %s [%s]", esp_err_to_name(err), err_msg);
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "HTTP Request failed"));
    }

    status = esp_http_client_get_status_code(client);

    mp_obj_t tuple[3];

    tuple[0] = mp_obj_new_int(status);
    if ((rqheader) && (rqheader_ptr)) tuple[1] = mp_obj_new_str(rqheader, rqheader_ptr);
    else tuple[1] = mp_const_none;

    if (rqbody_file) tuple[2] = mp_obj_new_str(tofile, strlen(tofile));
    else if ((rqbody) && (rqbody_ptr)) tuple[2] = mp_obj_new_str(rqbody, rqbody_ptr);
    else tuple[2] = mp_const_none;

    if (rqbody_file) fclose(rqbody_file);
    if (rqheader) free(rqheader);
    if (rqbody) free(rqbody);
    rqbody_file = NULL;
    rqheader = NULL;
    rqbody = NULL;

    return mp_obj_new_tuple(3, tuple);
}

//-----------------------------------------------------
void get_certificate(mp_obj_t cert, char *cert_pem_buf)
{
    if (MP_OBJ_IS_STR(cert)) {
        struct stat sb;
        char *fname = NULL;
        char certname[128] = {'\0'};

        fname = (char *)mp_obj_str_get_str(cert);
        esp_err_t res = physicalPath(fname, certname);
        if ((res != 0) || (strlen(certname) == 0)) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
        }
        if (stat(certname, &sb) != 0) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error opening certificate file"));
        }
        FILE *fhndl = fopen(certname, "rb");
        if (!fhndl) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error opening certificate file"));
        }

        if (cert_pem_buf) free(cert_pem_buf);
        cert_pem_buf = NULL;
        cert_pem_buf = calloc(sizeof(char), sb.st_size+16);
        if (cert_pem_buf == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error allocating certificate buffer"));
        }
        int rd_len = fread(cert_pem_buf, 1, sb.st_size, fhndl);
        fclose(fhndl);
        if (rd_len != sb.st_size) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error reading from certificate file"));
        }
    }
    else if (cert == mp_const_false) {
        if (cert_pem_buf) free(cert_pem_buf);
        cert_pem_buf = NULL;
    }
}

//--------------------------------------------------------------------------------------
STATIC mp_obj_t requests_GET(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    network_checkConnection();
    enum { ARG_url, ARG_file };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,   MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_file,                    MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *url = NULL;
    char *fname = NULL;

    url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

    if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
        // GET to file
        fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
    }

    mp_obj_t res = request(HTTP_METHOD_GET, false, NULL, url, fname);

    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(requests_GET_obj, 1, requests_GET);

//--------------------------------------------------------------------------------------
STATIC mp_obj_t requests_HEAD(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    network_checkConnection();
    enum { ARG_url };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,   MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *url = NULL;

    url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

    mp_obj_t res = request(HTTP_METHOD_HEAD, false, NULL, url, NULL);

    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(requests_HEAD_obj, 1, requests_HEAD);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t requests_POST(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    network_checkConnection();
    enum { ARG_url, ARG_params, ARG_file, ARG_multipart };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,        MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_params,     MP_ARG_REQUIRED | MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_file,                         MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_multipart,                    MP_ARG_BOOL, { .u_bool = false } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *url = NULL;
    char *fname = NULL;

    url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

    if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
        // GET to file
        fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
    }

    mp_obj_t res = request(HTTP_METHOD_POST, args[ARG_multipart].u_bool, args[ARG_params].u_obj, url, fname);

    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(requests_POST_obj, 1, requests_POST);

//--------------------------------------------------------------------------------------
STATIC mp_obj_t requests_PUT(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    network_checkConnection();
    enum { ARG_url, ARG_data };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *url = NULL;

    url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

    mp_obj_t res = request(HTTP_METHOD_PUT, false, args[ARG_data].u_obj, url, NULL);

    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(requests_PUT_obj, 1, requests_PUT);

//----------------------------------------------------------------------------------------
STATIC mp_obj_t requests_PATCH(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    network_checkConnection();
    enum { ARG_url, ARG_data };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,  MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *url = NULL;

    url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

    mp_obj_t res = request(HTTP_METHOD_PATCH, false, args[ARG_data].u_obj, url, NULL);

    return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(requests_PATCH_obj, 1, requests_PATCH);

//---------------------------------------------
STATIC mp_obj_t requests_debug(mp_obj_t dbg_in)
{
    rq_debug = mp_obj_is_true(dbg_in);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(requests_debug_obj, requests_debug);

//----------------------------------------------------
STATIC mp_obj_t requests_certificate(mp_obj_t cert_in)
{
    if (cert_in != mp_const_none) get_certificate(cert_in, cert_pem);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(requests_certificate_obj, requests_certificate);


//================================================================
STATIC const mp_rom_map_elem_t requests_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_requests) },

    { MP_ROM_QSTR(MP_QSTR_get),         MP_ROM_PTR(&requests_GET_obj) },
    { MP_ROM_QSTR(MP_QSTR_head),        MP_ROM_PTR(&requests_HEAD_obj) },
    { MP_ROM_QSTR(MP_QSTR_post),        MP_ROM_PTR(&requests_POST_obj) },
    { MP_ROM_QSTR(MP_QSTR_put),         MP_ROM_PTR(&requests_PUT_obj) },
    { MP_ROM_QSTR(MP_QSTR_patch),       MP_ROM_PTR(&requests_PATCH_obj) },
    { MP_ROM_QSTR(MP_QSTR_debug),       MP_ROM_PTR(&requests_debug_obj) },
    { MP_ROM_QSTR(MP_QSTR_certificate), MP_ROM_PTR(&requests_certificate_obj) },
};
STATIC MP_DEFINE_CONST_DICT(requests_module_globals, requests_module_globals_table);

//==========================================
const mp_obj_module_t mp_module_requests = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&requests_module_globals,
};

#endif
