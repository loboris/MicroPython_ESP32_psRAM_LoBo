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

#ifdef CONFIG_MICROPY_USE_WEBSERVER

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "py/mpstate.h"
#include "py/obj.h"
#include "extmod/vfs_native.h"
#include "extmod/vfs.h"
#include "libs/websrv.h"
#include "timeutils.h"

#include "dirent.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"

#include "esp_wifi.h"

//#include "lwip/sockets.h"
//#include "lwip/dns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

TaskHandle_t WebSrvTaskHandle = NULL;
QueueHandle_t websrv_mutex = NULL;
const char *WEBSRV_TAG = "[WebSrv]";


const static char http_html_hdr[] = "HTTP/1.1 200 OK\nContent-type: text/html\n\n";
const static char http_index_html[] = "<!DOCTYPE html>"
                                     "<html>\n"
                                     "<head>\n"
                                     "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                                     "  <style type=\"text/css\">\n"
                                     "    html, body, iframe { margin: 0; padding: 0; height: 100%; }\n"
                                     "    iframe { display: block; width: 100%; border: none; }\n"
                                     "  </style>\n"
                                     "<title>HELLO ESP32</title>\n"
                                     "</head>\n"
                                     "<body>\n"
                                     "<h1>Hello World, from ESP32!</h1>\n";


//-----------------------------------------------------------
static void http_server_netconn_serve(struct netconn *conn) {

    struct netbuf *inbuf;
    char *buf;
    u16_t buflen;
    err_t err;

    err = netconn_recv(conn, &inbuf);

    if (err == ERR_OK) {

        netbuf_data(inbuf, (void**)&buf, &buflen);

        // extract the first line, with the request
        char *first_line = strtok(buf, "\n");

        if (first_line) {
            // default page
            if (strstr(first_line, "GET / ")) {
                netconn_write(conn, http_html_hdr, sizeof(http_html_hdr) - 1, NETCONN_NOCOPY);
                netconn_write(conn, http_index_html, sizeof(http_index_html) - 1, NETCONN_NOCOPY);
                netconn_write(conn, "</body>\n</html>\n", 16, NETCONN_NOCOPY);
                ESP_LOGI(WEBSRV_TAG, "Got request from client");
            }
            else {
                ESP_LOGW(WEBSRV_TAG, "Unknown request type [%s]", first_line);
            }
        }
        else {
            ESP_LOGE(WEBSRV_TAG, "Unknown request");
        }
    }

    // close the connection and free the buffer
    netconn_close(conn);
    netbuf_delete(inbuf);
}


//========================================
void web_server_task(void *pvParameters) {

	struct netconn *conn, *newconn;
	err_t err;
	conn = netconn_new(NETCONN_TCP);
	netconn_bind(conn, NULL, 80);
	netconn_listen(conn);
    ESP_LOGD(WEBSRV_TAG, "HTTP Server listening...");
	do {
		err = netconn_accept(conn, &newconn);
	    ESP_LOGD(WEBSRV_TAG, "New client connected");
		if (err == ERR_OK) {
			http_server_netconn_serve(newconn);
			netconn_delete(newconn);
		}
	} while(err == ERR_OK);
	netconn_close(conn);
	netconn_delete(conn);
    ESP_LOGE(WEBSRV_TAG, "Netconn accept error, task halted!");
}


#endif
