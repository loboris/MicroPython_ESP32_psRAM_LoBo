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

#ifdef CONFIG_MICROPY_USE_OTA

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/socket.h>
#include <netdb.h>

#include "esp_err.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "mbedtls/md5.h"
#include "esp_ota_ops.h"
#include "rom/queue.h"
#include "rom/crc.h"
#include "soc/dport_reg.h"
#include "esp_log.h"
#include "esp_http_client.h"

#include "py/runtime.h"
#include "modmachine.h"
#include "mphalport.h"
#include "extmod/vfs_native.h"


#define BUFFSIZE 4096

static const char *TAG = "OTA_UPDATE";
static char *cert_pem = NULL;

extern void get_certificate(mp_obj_t cert, char *cert_pem_buf);

//----------------------------------------------------------------
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP Event: ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP Event: Connected");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP Event: Header sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            //ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            //ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP Event: Finish");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP Event: Disconnected");
            break;
    }
    return ESP_OK;
}

//-----------------------------------------------------------------------------------
static esp_err_t mpy_ota_update(const char *upd_url, uint8_t md5, uint8_t force_fact)
{
    if ((CONFIG_LOG_DEFAULT_LEVEL > ESP_LOG_WARN) && (CONFIG_MICRO_PY_LOG_LEVEL > ESP_LOG_WARN)){
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
        esp_log_level_set("esp_image", ESP_LOG_WARN);
        esp_log_level_set("TRANSPORT", ESP_LOG_WARN);
        esp_log_level_set("TRANS_SSL", ESP_LOG_WARN);
    }
	mp_hal_set_wdt_tmo();

	static esp_http_client_config_t http_client_config;
	memset(&http_client_config, 0, sizeof(esp_http_client_config_t));
	char remote_md5[33] = {0};
	char local_md5[33] = {0};
	char *ota_write_data = NULL; // ota data write buffer
	esp_err_t err = ESP_FAIL, errexit = ESP_FAIL;

	// update handle : set by esp_ota_begin(), must be freed via esp_ota_end() !
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG, "Find running partition failed !");
        goto exit;
    }

    if (force_fact) {
    	if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            ESP_LOGE(TAG, "Cannot update Factory partition from itself!");
            goto exit;
    	}
    	update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "MicroPython");
    }
    else update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Find update partition failed !");
        goto exit;
    }

    ota_write_data = malloc(BUFFSIZE+1);
    if (ota_write_data == NULL) {
        ESP_LOGE(TAG, "Error allocating buffer !");
        goto exit;
    }

   	ESP_LOGI(TAG, "Starting OTA update from '%s' to '%s' partition", running_partition->label, update_partition->label);

	mp_hal_reset_wdt();
    // Begin update
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
        goto exit;
    }

	mp_hal_reset_wdt();
    int expect_len = 0;
    int data_read = 0;
    esp_http_client_handle_t client = NULL;

    if (md5) {
   	   	// === Get the image MD5 file from server ===
        char http_request[strlen(upd_url)+8];
        strcpy(http_request, upd_url);
        strcat(http_request, ".md5");
        http_client_config.url = http_request;
        http_client_config.cert_pem = cert_pem;
        http_client_config.event_handler = _http_event_handler;
        //http_client_config.buffer_size = BUFFSIZE;

        client = esp_http_client_init(&http_client_config);
        if (client == NULL) {
            ESP_LOGE(TAG, "Failed to initialise HTTP connection");
            goto exit;
        }
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            esp_http_client_cleanup(client);
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            goto exit_client;
        }

        if (esp_http_client_fetch_headers(client) == ESP_FAIL) {
            ESP_LOGE(TAG, "Error fetching headers");
            goto exit_client;
        }

        data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
        if (data_read >= 32) strncpy(remote_md5, ota_write_data, 32);
        else {
            ESP_LOGE(TAG, "Remote MD5 requested but not received");
            goto exit_client;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        client = NULL;
   	}

   	// === Connect to http server to get the image file ===
    http_client_config.url = upd_url;
    http_client_config.cert_pem = cert_pem;
    http_client_config.event_handler = _http_event_handler;
    //http_client_config.buffer_size = BUFFSIZE;

    if (client == NULL) client = esp_http_client_init(&http_client_config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        goto exit;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        goto exit_client;
    }
    err = esp_http_client_fetch_headers(client);
    if (err == ESP_FAIL) {
        ESP_LOGE(TAG, "Error fetching headers");
        goto exit_client;
    }

	mp_hal_reset_wdt();

    expect_len = esp_http_client_get_content_length(client);
    if (expect_len > 0) {
        ESP_LOGI(TAG, "Update image size: %d bytes", expect_len);
    }
    else {
        ESP_LOGW(TAG, "Cannot determine image size");
    }

    data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
    if (data_read <= 0) {
        vTaskDelay(5);
        data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
    }
    if (data_read <= 0) {
        ESP_LOGE(TAG, "Error reading from server (%d)", data_read);
        goto exit_client;
    }

    // We have some data received, check for image magic byte
    if (ota_write_data[0] != 0xE9) {
        ESP_LOGE(TAG, "Error: OTA image has invalid magic byte (%02X <> E9)", ota_write_data[0]);
        goto exit_client;
    }

    // Start writing data
   	ESP_LOGI(TAG, "Writing to '%s' partition at offset 0x%x", update_partition->label, update_partition->address);

    unsigned char md5_byte_array[16] = {0};
    mbedtls_md5_context ctx;
    mbedtls_md5_init( &ctx );
    mbedtls_md5_starts( &ctx );
	int binary_file_length = 0;  // image total length

	while (data_read > 0) {
		mp_hal_reset_wdt();
        err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
        mbedtls_md5_update( &ctx, (const unsigned char *)ota_write_data, data_read);
        if (err != ESP_OK) {
        	mp_hal_stdout_tx_newline();
            ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
            goto exit_client;
        }
        binary_file_length += data_read;
        mp_printf(&mp_plat_print, "%s Received %d bytes\r", TAG, binary_file_length);

        data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
        if ((expect_len > 0) && ((binary_file_length + data_read) > expect_len)) {
    		ESP_LOGE(TAG, "More than expected bytes received %u > %u\n", binary_file_length+data_read, expect_len);
    		goto exit_client;
        }
        if ((binary_file_length + data_read) > update_partition->size) {
    		ESP_LOGE(TAG, "Received more bytes than the partition size: %u > %u\n", binary_file_length+data_read, update_partition->size);
    		goto exit_client;
		}
    }

	mbedtls_md5_finish( &ctx, md5_byte_array );
    mbedtls_md5_free( &ctx );
    for (int i = 0; i<16; i++){
        sprintf(local_md5+(i*2),"%02x", md5_byte_array[i]);
    }

    mp_printf(&mp_plat_print,"                                                         \n");
    ESP_LOGI(TAG, "Connection closed, all packets received");
	ESP_LOGI(TAG, "Image written, total length = %d bytes\n", binary_file_length);
	if ((expect_len > 0) && (expect_len != binary_file_length)) {
		ESP_LOGE(TAG, "Expected image length not equal to received length: %u <> %u\n", expect_len, binary_file_length);
		goto exit_client;
	}
   	if (md5) {
		if (strlen(remote_md5) == 32) {
			if (strncmp(remote_md5, local_md5, 32) == 0) {
				ESP_LOGI(TAG, "MD5 Checksum check PASSED.");
			}
			else {
				ESP_LOGE(TAG, "MD5 Checksum check FAILED!");
				goto exit_client;
			}
		}
   	}

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed! err=0x%x", err);
        goto exit_client;
    }
	mp_hal_reset_wdt();
    // === Set boot partition ===
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA set_boot_partition failed! err=0x%x", err);
        goto exit_client;
    }
    ESP_LOGW(TAG, "On next reboot the system will be started from '%s' partition", update_partition->label);
    errexit = ESP_OK;
    goto exit;

exit_client:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

exit:
	if (ota_write_data) free(ota_write_data);

	return errexit;
}

//------------------------------------------------------------------------
static esp_err_t mpy_ota_fileupdate(const char *fname, uint8_t force_fact)
{
    if ((CONFIG_LOG_DEFAULT_LEVEL > ESP_LOG_WARN) && (CONFIG_MICRO_PY_LOG_LEVEL > ESP_LOG_WARN)){
        esp_log_level_set("esp_image", ESP_LOG_WARN);
    }
	mp_hal_set_wdt_tmo();

	char *ota_write_data = NULL; // ota data write buffer
	esp_err_t err = ESP_FAIL, errexit = ESP_FAIL;
	struct stat sb;
	FILE *fhndl = NULL;
	char file_md5[33] = {0};
	char local_md5[33] = {0};
   	char md5_fname[strlen(fname)+8];

	// update handle : set by esp_ota_begin(), must be freed via esp_ota_end() !
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG, "Find running partition failed !");
        goto exit;
    }

    if (force_fact) {
    	if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            ESP_LOGE(TAG, "Cannot update Factory partition from itself!");
            goto exit;
    	}
    	update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "MicroPython");
    }
    else update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Find update partition failed !");
        goto exit;
    }

    ota_write_data = malloc(BUFFSIZE+1);
    if (ota_write_data == NULL) {
        ESP_LOGE(TAG, "Error allocating buffer !");
        goto exit;
    }

   	ESP_LOGI(TAG, "Starting OTA update from '%s' to '%s' partition", running_partition->label, update_partition->label);

	mp_hal_reset_wdt();
    // Begin update
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
        goto exit;
    }

	mp_hal_reset_wdt();
   	// Check if md5 file exists
   	strcpy(md5_fname, fname);
	if (stat(md5_fname, &sb) == 0) {
		int len = sb.st_size;
	    if ((len >= 32) && (len < 100)) {
		    fhndl = fopen(md5_fname, "rb");
			if (fhndl) {
				len = fread(ota_write_data, 1, 32, fhndl);
				if (len == 32) {
					ota_write_data[32] = '\0';
					strncpy(file_md5, ota_write_data, 33);
			    	ESP_LOGI(TAG, "MD5 file found");
				}
				fclose(fhndl);
				fhndl = NULL;
			}
	    }
	}
	if (strlen(file_md5) != 32) {
    	ESP_LOGI(TAG, "MD5 file NOT found");
	}

   	// Open the update file
	if (stat(fname, &sb) != 0) {
        ESP_LOGE(TAG, "Error opening update file !");
		goto exit;
	}
	int expect_len = sb.st_size;
    if (expect_len > 100000) {
    	ESP_LOGI(TAG, "Update image size: %d bytes", expect_len);
    }
    else {
        ESP_LOGE(TAG, "File size too small !");
		goto exit;
    }

    fhndl = fopen(fname, "rb");
	if (!fhndl) {
        ESP_LOGE(TAG, "Error opening update file !");
		goto exit;
	}

	int rd_len = fread(ota_write_data, 1, BUFFSIZE, fhndl);
	if (rd_len <= 0) {
        ESP_LOGE(TAG, "Error reading from update file !");
		goto exit;
	}

    if (ota_write_data[0] != 0xE9) {
        ESP_LOGE(TAG, "Error: OTA image has invalid magic byte!");
        goto exit;
    }

    // Start writing data
   	ESP_LOGI(TAG, "Writing to '%s' partition at offset 0x%x", update_partition->label, update_partition->address);

   	unsigned char md5_byte_array[16] = {0};
    mbedtls_md5_context ctx;
    mbedtls_md5_init( &ctx );
    mbedtls_md5_starts( &ctx );
	int binary_file_length = 0;  // image total length
	int remaining = expect_len;

	while (rd_len > 0) {
		mp_hal_reset_wdt();
        err = esp_ota_write( update_handle, (const void *)ota_write_data, rd_len);
        mbedtls_md5_update( &ctx, (const unsigned char *)ota_write_data, rd_len);
        if (err != ESP_OK) {
        	mp_hal_stdout_tx_newline();
            ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
            goto exit;
        }
        binary_file_length += rd_len;
        remaining -= rd_len;
        if (remaining <= 0) break;

        rd_len = fread(ota_write_data, 1, BUFFSIZE, fhndl);
        if ((binary_file_length + rd_len) > expect_len) {
    		ESP_LOGW(TAG, "More than expected bytes read %u > %u [%d]\n", binary_file_length+rd_len, expect_len, rd_len);
    		rd_len = expect_len - binary_file_length;
    		goto exit;
        }
        if ((binary_file_length + rd_len) > update_partition->size) {
    		ESP_LOGW(TAG, "Update file bigger than the partition size: %u > %u\n", binary_file_length+rd_len, update_partition->size);
    		goto exit;
		}
    }
    mbedtls_md5_finish( &ctx, md5_byte_array );
    mbedtls_md5_free( &ctx );
    for (int i = 0; i<16; i++){
        sprintf(local_md5+(i*2),"%02x", md5_byte_array[i]);
    }

	ESP_LOGI(TAG, "Image written, total length = %d bytes\n", binary_file_length);
	if (expect_len != binary_file_length) {
		ESP_LOGE(TAG, "Read size not equal to file size: %u <> %u\n", expect_len, binary_file_length);
		goto exit;
	}
	if (strlen(file_md5) == 32) {
		if (strncmp(file_md5, local_md5, 32) == 0) {
			ESP_LOGI(TAG, "MD5 Checksum check PASSED.");
		}
		else {
			ESP_LOGE(TAG, "MD5 Checksum check FAILED!");
			goto exit;
		}
	}

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed! err=0x%x", err);
        goto exit;
    }

    mp_hal_reset_wdt();
    // === Set boot partition ===
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA set_boot_partition failed! err=0x%x", err);
        goto exit;
    }
    ESP_LOGW(TAG, "On next reboot the system will be started from '%s' partition", update_partition->label);
    errexit = ESP_OK;

exit:
	if (fhndl) fclose(fhndl);
	if (ota_write_data) free(ota_write_data);

	return errexit;
}

//------------------------------------------------------------------------------------------
STATIC mp_obj_t mod_ota_start(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_url,  ARG_restart, ARG_md5, ARG_forceFact, ARG_cert };
    const mp_arg_t allowed_args[] = {
			{ MP_QSTR_url,          MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_restart,      MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false} },
			{ MP_QSTR_md5,          MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false} },
			{ MP_QSTR_forceFactory, MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false} },
            { MP_QSTR_certificate,  MP_ARG_KW_ONLY  | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);

    if (cert_pem) free(cert_pem);
    cert_pem = NULL;

    get_certificate(args[ARG_cert].u_obj, cert_pem);

    esp_err_t res = mpy_ota_update(url, args[ARG_md5].u_bool, args[ARG_forceFact].u_bool);

    if (cert_pem) free(cert_pem);
    cert_pem = NULL;

    if (res != ESP_OK) return mp_const_false;

    if (args[ARG_restart].u_bool) {
		prepareSleepReset(1, NULL);
		esp_restart(); // This function does not return.
	}
	return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mod_ota_start_obj, 0, mod_ota_start);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t mod_ota_fromfile(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
	enum { ARG_file, ARG_restart, ARG_forceFact };
    const mp_arg_t allowed_args[] = {
			{ MP_QSTR_file,    		MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
			{ MP_QSTR_restart, 						  MP_ARG_BOOL, {.u_bool = false} },
			{ MP_QSTR_forceFactory,	MP_ARG_KW_ONLY  | MP_ARG_BOOL, {.u_bool = false} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *fname = NULL;
    char fullname[128] = {'\0'};

    fname = (char *)mp_obj_str_get_str(args[ARG_file].u_obj);

    esp_err_t res = physicalPath(fname, fullname);
    if ((res != 0) || (strlen(fullname) == 0)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "Error resolving file name"));
    }

    res = mpy_ota_fileupdate(fullname, args[ARG_forceFact].u_bool);

    if (res != ESP_OK) return mp_const_false;

    if (args[ARG_restart].u_bool) {
		prepareSleepReset(1, NULL);
		esp_restart(); // This function does not return.
	}
	return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mod_ota_fromfile_obj, 0, mod_ota_fromfile);

//---------------------------------------------------------------------------------------------
STATIC mp_obj_t mod_ota_set_boot(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t allowed_args[] = {
			{ MP_QSTR_partition, MP_ARG_REQUIRED | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    char *part_name = NULL;

    part_name = (char *)mp_obj_str_get_str(args[0].u_obj);

    const esp_partition_t *boot_part1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, part_name);
    const esp_partition_t *boot_part2 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, part_name);
    const esp_partition_t *boot_part3 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, part_name);
    if ((boot_part1 == NULL) && (boot_part2 == NULL) && (boot_part3 == NULL)) {
		ESP_LOGE(TAG, "Partition not found !");
		return mp_const_false;
    }

    // === Set boot partition ===
    char sptype[16] = {'\0'};
    char splabel[16] = {'\0'};
    esp_err_t err = ESP_FAIL;

    if (boot_part1 != NULL) {
    	sprintf(sptype,"Factory");
    	sprintf(splabel, boot_part1->label);
    	err = esp_ota_set_boot_partition(boot_part1);
    }
    else if (boot_part2 != NULL) {
    	sprintf(sptype,"OTA_1");
    	sprintf(splabel, boot_part2->label);
    	err = esp_ota_set_boot_partition(boot_part2);
    }
    else if (boot_part3 != NULL) {
    	sprintf(sptype,"OTA_2");
    	sprintf(splabel, boot_part3->label);
    	err = esp_ota_set_boot_partition(boot_part3);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA set_boot_partition failed! err=0x%x", err);
        return mp_const_false;
    }
    ESP_LOGW(TAG, "On next reboot the system will be started from '%s' partition (%s)", splabel, sptype);

    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mod_ota_set_boot_obj, 0, mod_ota_set_boot);


//===========================================================
STATIC const mp_rom_map_elem_t ota_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start),			MP_ROM_PTR(&mod_ota_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_fromfile),		MP_ROM_PTR(&mod_ota_fromfile_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_bootpart),	MP_ROM_PTR(&mod_ota_set_boot_obj) },
};
STATIC MP_DEFINE_CONST_DICT(ota_module_globals, ota_module_globals_table);

//======================================
const mp_obj_module_t mp_module_ota = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&ota_module_globals,
};


#endif

