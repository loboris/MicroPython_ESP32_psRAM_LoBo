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

#include "esp_http_client.h"
#include "esp_wifi_types.h"
#include "tcpip_adapter.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "extmod/vfs_native.h"

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

/* Root cert for howsmyssl.com, taken from howsmyssl_com_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
//extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
//extern const char howsmyssl_com_root_cert_pem_end[]   asm("_binary_howsmyssl_com_root_cert_pem_end");

//---------------------------------------------------------
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            if (rq_debug) ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
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

//---------------------
static void http_rest()
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    // POST
    const char *post_data = "field1=value1&field2=value2";
    esp_http_client_set_url(client, "http://httpbin.org/post");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    //PUT
    esp_http_client_set_url(client, "http://httpbin.org/put");
    esp_http_client_set_method(client, HTTP_METHOD_PUT);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP PUT Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP PUT request failed: %s", esp_err_to_name(err));
    }

    //PATCH
    esp_http_client_set_url(client, "http://httpbin.org/patch");
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_post_field(client, NULL, 0);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP PATCH Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP PATCH request failed: %s", esp_err_to_name(err));
    }

    //DELETE
    esp_http_client_set_url(client, "http://httpbin.org/delete");
    esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP DELETE Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP DELETE request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

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


//--------------------
static void checkConnection()
{
    tcpip_adapter_ip_info_t info;
    tcpip_adapter_get_ip_info(WIFI_IF_STA, &info);
    if (info.ip.addr == 0) {
        #ifdef CONFIG_MICROPY_USE_GSM
        if (ppposStatus() != GSM_STATE_CONNECTED) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "No Internet connection"));
        }
        #else
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "No Internet connection"));
        #endif
    }
}

//-----------------------------------
STATIC mp_obj_t mod_requests_test() {
    ESP_LOGI(TAG, "Connected to AP, begin http example");
    http_rest();
    http_auth_basic();
    http_auth_basic_redirect();
    http_auth_digest();
    http_relative_redirect();
    http_absolute_redirect();
    https();
    http_redirect_to_https();
    http_download_chunk();
    http_perform_as_stream_reader();
    ESP_LOGI(TAG, "Finish http example");
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_requests_test_obj, mod_requests_test);

//--------------------------------------------------------------------------------------
STATIC mp_obj_t requests_GET(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    checkConnection();
    enum { ARG_url, ARG_file };
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_url,      MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_obj = mp_const_none } },
        { MP_QSTR_file,                       MP_ARG_OBJ, { .u_obj = mp_const_none } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
    esp_log_level_set("TRANS_TCP", ESP_LOG_WARN);
    esp_log_level_set("TRANS_SSL", ESP_LOG_WARN);
    int status;
    char *url = NULL;
    char *fname = NULL;
    char fullname[128] = {'\0'};

    url = (char *)mp_obj_str_get_str(args[ARG_url].u_obj);

    rqbody_file = NULL;
    if (MP_OBJ_IS_STR(args[ARG_file].u_obj)) {
        // GET to file
        fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);
        int res = physicalPath(fname, fullname);
        if ((res != 0) || (strlen(fullname) == 0)) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
        }
        rqbody_file = fopen(fullname, "wb");
        if (rqbody_file == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error opening file"));
        }
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (rqheader) free(rqheader);
    if (rqbody) free(rqbody);
    rqheader = NULL;
    rqheader_len = 0;
    rqbody = NULL;
    rqbody_len = 0;
    rqbody_ptr = 0;
    rqbody_ok = true;
    // GET
    MP_THREAD_GIL_EXIT();
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    MP_THREAD_GIL_ENTER();
    if (err != ESP_OK) {
        if (rqbody_file) fclose(rqbody_file);
        if (rqheader) free(rqheader);
        if (rqbody) free(rqbody);
        rqbody_file = NULL;
        rqheader = NULL;
        rqbody = NULL;
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "HTTP GET request failed"));
    }

    status = esp_http_client_get_status_code(client);

    mp_obj_t tuple[3];

    tuple[0] = mp_obj_new_int(status);
    if ((rqheader) && (rqheader_ptr)) tuple[1] = mp_obj_new_str(rqheader, rqheader_ptr);
    else tuple[1] = mp_const_none;

    if (rqbody_file) tuple[2] = mp_obj_new_str(fname, strlen(fname));
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
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(requests_GET_obj, 1, requests_GET);


//================================================================
STATIC const mp_rom_map_elem_t requests_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_requests) },

    { MP_ROM_QSTR(MP_QSTR_test),        MP_ROM_PTR(&mod_requests_test_obj) },
    { MP_ROM_QSTR(MP_QSTR_get),         MP_ROM_PTR(&requests_GET_obj) },
};
STATIC MP_DEFINE_CONST_DICT(requests_module_globals, requests_module_globals_table);

//==========================================
const mp_obj_module_t mp_module_requests = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&requests_module_globals,
};

#endif
