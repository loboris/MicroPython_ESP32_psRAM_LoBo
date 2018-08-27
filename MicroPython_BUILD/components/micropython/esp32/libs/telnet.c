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
 * This file is based on 'telnet' from Pycom Limited.
 *
 * Author: LoBo, loboris@gmail.com
 * Copyright (c) 2017, LoBo
 */

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_TELNET

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "py/mpconfig.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "mpversion.h"
#include "telnet.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/******************************************************************************
 DEFINE PRIVATE CONSTANTS
 ******************************************************************************/
#define TELNET_PORT                         23
// rxRindex and rxWindex must be uint8_t and TELNET_RX_BUFFER_SIZE == 256
#define TELNET_RX_BUFFER_SIZE               256
#define TELNET_MAX_CLIENTS                  1
#define TELNET_TX_RETRIES_MAX               50
#define TELNET_WAIT_TIME_MS                 10
#define TELNET_LOGIN_RETRIES_MAX            3

#define SE 240
#define AYT 246
#define IAC 255
#define SB 250
#define WILL 251
#define WONT 252
#define DO 253
#define DONT 254
#define TRANSMIT_BINARY 0
#define ECHO 1
#define SUPPRESS_GO_AHEAD 3
#define LINEMODE 34
#define MODE 1
#define EDIT 1

TaskHandle_t TelnetTaskHandle = NULL;
uint32_t telnet_stack_size;
int telnet_timeout = TELNET_DEF_TIMEOUT_MS;

/******************************************************************************
 DEFINE PRIVATE TYPES
 ******************************************************************************/
typedef enum {
    E_TELNET_RESULT_OK = 0,
    E_TELNET_RESULT_AGAIN,
    E_TELNET_RESULT_FAILED
} telnet_result_t;

typedef enum {
    E_TELNET_STE_SUB_WELCOME,
    E_TELNET_STE_SUB_SND_USER_OPTIONS,
    E_TELNET_STE_SUB_REQ_USER,
    E_TELNET_STE_SUB_GET_USER,
    E_TELNET_STE_SUB_REQ_PASSWORD,
    E_TELNET_STE_SUB_SND_PASSWORD_OPTIONS,
    E_TELNET_STE_SUB_GET_PASSWORD,
    E_TELNET_STE_SUB_INVALID_LOGIN,
    E_TELNET_STE_SUB_SND_REPL_OPTIONS,
    E_TELNET_STE_SUB_LOGIN_SUCCESS
} telnet_connected_substate_t;

typedef union {
    telnet_connected_substate_t connected;
} telnet_substate_t;

typedef struct {
    uint8_t             *rxBuffer;
    uint64_t            timeout;
    telnet_state_t      state;
    telnet_substate_t   substate;
    int32_t             sd;
    int32_t             n_sd;

    // rxRindex and rxWindex must be uint8_t and TELNET_RX_BUFFER_SIZE == 256
    uint8_t             rxWindex;
    uint8_t             rxRindex;

    // used to store incoming chars in cases the reception needs to be completed later
    uint8_t             rxIncompleteLen;

    uint8_t             txRetries;
    uint8_t             loginRetries;
    bool                enabled;
    bool                credentialsValid;
    bool                binary_mode;
} telnet_data_t;



QueueHandle_t telnet_mutex = NULL;

static uint8_t telnet_stop = 0;
char telnet_user[TELNET_USER_PASS_LEN_MAX + 1];
char telnet_pass[TELNET_USER_PASS_LEN_MAX + 1];

static telnet_data_t telnet_data = {0};

static const char* telnet_welcome_msg       = "MicroPython " MICROPY_GIT_TAG " - " MICROPY_BUILD_DATE " on " MICROPY_HW_BOARD_NAME " with " MICROPY_HW_MCU_NAME "\r\n";
static const char* telnet_request_user      = "Login as: ";
static const char* telnet_request_password  = "Password: ";
static const char* telnet_invalid_login     = "\r\nInvalid credentials, try again.\r\n";

#if TELNET_LOGIN_MSG_LEN_MAX > 0
static char telnet_login_msg[TELNET_LOGIN_MSG_LEN_MAX+1];
#else
const char* telnet_login_msg                = "\r\nLogin succeeded!\r\nType \"help()\" for more information.\r\n";
#endif
char *telnet_login_success = telnet_login_msg;

static const uint8_t telnet_options_user[]  = { IAC, WONT, ECHO, IAC, WONT, SUPPRESS_GO_AHEAD, IAC, WILL, LINEMODE };
static const uint8_t telnet_options_pass[]  = { IAC, WILL, ECHO, IAC, WONT, SUPPRESS_GO_AHEAD, IAC, WILL, LINEMODE };
static const uint8_t telnet_options_repl[]  = { IAC, WILL, ECHO, IAC, WILL, SUPPRESS_GO_AHEAD, IAC, WONT, LINEMODE };


//--------------------------------
static void _telnet_reset (void) {
    // close the connection and start all over again
    closesocket(telnet_data.n_sd);
    telnet_data.n_sd = -1;
    closesocket(telnet_data.sd);
    telnet_data.sd = -1;
    telnet_data.state = E_TELNET_STE_START;
}

//------------------------------------------
static void telnet_wait_for_enabled (void) {
    // Init telnet's data
    telnet_data.n_sd = -1;
    telnet_data.sd   = -1;

    // Check if the telnet service has been enabled
    if (telnet_data.enabled) {
        telnet_data.state = E_TELNET_STE_START;
    }
}

//---------------------------------------
static bool telnet_create_socket (void) {
    struct sockaddr_in sServerAddress;
    int32_t result;

    // open a socket for telnet
    telnet_data.sd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (telnet_data.sd > 0) {
        // add the socket to the network administration
        //modusocket_socket_add(telnet_data.sd, false);

        // enable non-blocking mode
        uint32_t option = fcntl(telnet_data.sd, F_GETFL, 0);
        option |= O_NONBLOCK;
        fcntl(telnet_data.sd, F_SETFL, option);

        // enable address reusing
        option = 1;
        result = setsockopt(telnet_data.sd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

        // bind the socket to a port number
        sServerAddress.sin_family = AF_INET;
        sServerAddress.sin_addr.s_addr = INADDR_ANY;
        sServerAddress.sin_len = sizeof(sServerAddress);
        sServerAddress.sin_port = htons(TELNET_PORT);

        result = bind(telnet_data.sd, (const struct sockaddr *)&sServerAddress, sizeof(sServerAddress));

        // start listening
        result |= listen (telnet_data.sd, TELNET_MAX_CLIENTS - 1);

        if (!result) {
            return true;
        }
        closesocket(telnet_data.sd);
        telnet_data.sd = -1;
    }

    return false;
}

//---------------------------------------------
static void telnet_wait_for_connection (void) {
    socklen_t  in_addrSize;
    struct sockaddr_in  sClientAddress;

    // accepts a connection from a TCP client, if there is any, otherwise returns EAGAIN
    telnet_data.n_sd = accept(telnet_data.sd, (struct sockaddr *)&sClientAddress, (socklen_t *)&in_addrSize);
    if (telnet_data.n_sd < 0 && errno == EAGAIN) {
        return;
    } else {
        if (telnet_data.n_sd <= 0) {
            // error
        	//printf("[Telnet] Wait Connection Error\n");
            _telnet_reset();
            return;
        }

        // close the listening socket, we don't need it anymore
        closesocket(telnet_data.sd);
        telnet_data.sd = -1;

        // add the new socket to the network administration
        //modusocket_socket_add(telnet_data.n_sd, false);

        // enable non-blocking mode
        uint32_t option = fcntl(telnet_data.n_sd, F_GETFL, 0);
        option |= O_NONBLOCK;
        fcntl(telnet_data.n_sd, F_SETFL, option);

        // client connected, so go on
        telnet_data.rxWindex = 0;
        telnet_data.rxRindex = 0;
        telnet_data.txRetries = 0;
        telnet_data.rxIncompleteLen = 0;

        telnet_data.state = E_TELNET_STE_CONNECTED;
        telnet_data.substate.connected = E_TELNET_STE_SUB_WELCOME;
        telnet_data.credentialsValid = true;
        telnet_data.loginRetries = 0;
        telnet_data.timeout = mp_hal_ticks_ms();
        telnet_data.binary_mode = false;
    }
}

//-------------------------------------------------------------------------
static telnet_result_t telnet_send_non_blocking (void *data, int32_t Len) {
    if (send(telnet_data.n_sd, data, Len, 0) > 0) {
        telnet_data.txRetries = 0;
        return E_TELNET_RESULT_OK;
    }
    else if ((TELNET_TX_RETRIES_MAX >= ++telnet_data.txRetries) && (errno == EAGAIN)) {
        return E_TELNET_RESULT_AGAIN;
    }
    else {
        // error
    	printf("[Telnet] Send Error\n");
        _telnet_reset();
        return E_TELNET_RESULT_FAILED;
    }
}

//--------------------------------------------------------------------------------
static bool telnet_send_with_retries (int32_t sd, const void *pBuf, int32_t len) {
    int32_t retries = 0;
    uint32_t delay = TELNET_WAIT_TIME_MS;

	do {
		if (send(sd, pBuf, len, 0) > 0) {
			return true;
		}
		else if (EAGAIN != errno) {
			return false;
		}
		// start with the default delay and increment it on each retry
		vTaskDelay(delay++ / portTICK_PERIOD_MS);
	} while (++retries <= TELNET_TX_RETRIES_MAX);

	return false;
}

//--------------------------------------------------
static uint8_t telnet_get_reply_verb(uint8_t verb) {
    if (verb < DO) {
        // translate a will into do and a won't into don't
        return verb + (DO - WILL);
    } else {
        // if not, translate a do into will and don't into won't
        return verb - (DO - WILL);
    }
}

//------------------------------------------------------------------------------------------------
static int telnet_process_IAC (uint8_t **strR, uint8_t **strW, int32_t *len, uint32_t remaining) {
    if (remaining >= 2)  {
        switch (*((*strR) + 1)) {
            case IAC:
                // double IAC char (0xFF) means escaped 0xFF
                **strW = 0xFF;
                (*strW)++;
                (*strR) += 2;
                (*len)--;
                return 0;

            case AYT:
                // reply to the AYT with an echo of the IAC AYT
                telnet_send_with_retries (telnet_data.n_sd, (char *) *strR, 2);
                (*strR) += 2;
                (*len) -= 2;
                return 0;
        }
    }

    if (remaining >= 3) {
        if (*((*strR) + 2) == TRANSMIT_BINARY) {
            uint8_t option = *((*strR) + 1);
            if (option == WILL) telnet_data.binary_mode = true;
            if (option == WONT) telnet_data.binary_mode = false;
            *((*strR) + 1) = telnet_get_reply_verb(option);
            telnet_send_with_retries (telnet_data.n_sd, (char *) *strR, 3);
        }
        (*strR) += 3;
        (*len) -= 3;
        return 0;
    } else {
        // no enough characters to continue
        *len -= remaining;
        return remaining;
    }
    return 0;
}

//-----------------------------------------------------------
static void telnet_parse_input (uint8_t *str, int32_t *len) {
    int32_t b_len = *len;
    uint8_t *b_str = str - telnet_data.rxIncompleteLen;

    *len += telnet_data.rxIncompleteLen;
    b_len = *len;

    for (uint8_t *_str = b_str; _str < b_str + b_len; ) {
        uint8_t ch = *_str;
        if (ch == IAC) {
            uint32_t remaining = b_len - (_str - b_str);
            telnet_data.rxIncompleteLen = telnet_process_IAC(&_str, &str, len, remaining);
            if (telnet_data.rxIncompleteLen > 0) {
                break;
            }
            continue;
        }

        if (telnet_data.binary_mode == true) {
            *str++ = *_str++;
            continue;
        }

        // in this case the server is not operating on binary mode
        if (ch > 127 || ch == 0 || (telnet_data.state == E_TELNET_STE_LOGGED_IN && ch == mp_interrupt_char)) {
            if (ch == mp_interrupt_char) {
                mp_keyboard_interrupt();
            }
            // skip this char
            (*len)--;
            _str++;
        } else {
            *str++ = *_str++;
        }
    }
}

//-----------------------------------------------------------------------------------------------------
static void telnet_send_and_proceed (void *data, int32_t Len, telnet_connected_substate_t next_state) {
    if (E_TELNET_RESULT_OK == telnet_send_non_blocking(data, Len)) {
        telnet_data.substate.connected = next_state;
    }
}

//-------------------------------------------------------------------------------------------------
static telnet_result_t telnet_recv_text_non_blocking (void *buff, int32_t Maxlen, int32_t *rxLen) {
    *rxLen = recv(telnet_data.n_sd, buff, Maxlen, 0);
    // if there's data received, parse it
    if (*rxLen > 0) {
        telnet_data.timeout = mp_hal_ticks_ms();
        telnet_parse_input (buff, rxLen);
        if (*rxLen > 0) {
            return E_TELNET_RESULT_OK;
        }
    }
    else if (errno != EAGAIN) {
        // error
    	printf("[Telnet] Connection terminated\n");
        _telnet_reset();
        return E_TELNET_RESULT_FAILED;
    }
    return E_TELNET_RESULT_AGAIN;
}

//---------------------------------
static void telnet_process (void) {
    int32_t rxLen;
    int32_t maxLen = (telnet_data.rxWindex >= telnet_data.rxRindex) ? (TELNET_RX_BUFFER_SIZE - telnet_data.rxWindex) :
                                                                   ((telnet_data.rxRindex - telnet_data.rxWindex) - 1);
    // to avoid an overrrun
    maxLen = (telnet_data.rxRindex == 0) ? (maxLen - 1) : maxLen;

    if (maxLen > 0) {
        if (E_TELNET_RESULT_OK == telnet_recv_text_non_blocking(&telnet_data.rxBuffer[telnet_data.rxWindex], maxLen, &rxLen)) {
            // rxWindex must be uint8_t and TELNET_RX_BUFFER_SIZE == 256 so that it wraps around automatically
            telnet_data.rxWindex = telnet_data.rxWindex + rxLen;
        }
    }
}

//----------------------------------------------------------------------
static int telnet_process_credential (char *credential, int32_t rxLen) {
    telnet_data.rxWindex += rxLen;
    if (telnet_data.rxWindex >= TELNET_USER_PASS_LEN_MAX) {
        telnet_data.rxWindex = TELNET_USER_PASS_LEN_MAX;
    }

    uint8_t *p = telnet_data.rxBuffer + TELNET_USER_PASS_LEN_MAX;
    // if a '\r' is found, or the length exceeds the max username length
    if ((p = memchr(telnet_data.rxBuffer, '\r', telnet_data.rxWindex)) || (telnet_data.rxWindex >= TELNET_USER_PASS_LEN_MAX)) {
        uint8_t len = p - telnet_data.rxBuffer;

        telnet_data.rxWindex = 0;
        if ((len > 0) && (memcmp(credential, telnet_data.rxBuffer, MAX(len, strlen(credential))) == 0)) {
            return 1;
        }
        return -1;
    }
    return 0;
}

//--------------------------------------
static void telnet_reset_buffer (void) {
    // erase any characters present in the current line
    memset (telnet_data.rxBuffer, '\b', TELNET_RX_BUFFER_SIZE / 2);
    telnet_data.rxWindex = TELNET_RX_BUFFER_SIZE / 2;
    // fake an "enter" key pressed to display the prompt
    telnet_data.rxBuffer[telnet_data.rxWindex++] = '\r';
}


// =======================================================
// = The following functions are called from other tasks =
// = Mutex is used to synchronize with telnet_run        =
// =======================================================

//======================
int telnet_run (void) {
    int32_t rxLen;
    if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return -1;

    if (telnet_stop) return -2;

    switch (telnet_data.state) {
        case E_TELNET_STE_DISABLED:
            telnet_wait_for_enabled();
            break;
        case E_TELNET_STE_START:
            if (/*wlan_is_connected() && */ telnet_create_socket()) {
                telnet_data.state = E_TELNET_STE_LISTEN;
            }
            break;
        case E_TELNET_STE_LISTEN:
            telnet_wait_for_connection();
            break;
        case E_TELNET_STE_CONNECTED:
            switch (telnet_data.substate.connected) {
            case E_TELNET_STE_SUB_WELCOME:
                telnet_send_and_proceed((void *)telnet_welcome_msg, strlen(telnet_welcome_msg), E_TELNET_STE_SUB_SND_USER_OPTIONS);
                break;
            case E_TELNET_STE_SUB_SND_USER_OPTIONS:
                telnet_send_and_proceed((void *)telnet_options_user, sizeof(telnet_options_user), E_TELNET_STE_SUB_REQ_USER);
                break;
            case E_TELNET_STE_SUB_REQ_USER:
                // to catch any left over characters from the previous actions
                telnet_recv_text_non_blocking(telnet_data.rxBuffer, TELNET_RX_BUFFER_SIZE, &rxLen);
                telnet_send_and_proceed((void *)telnet_request_user, strlen(telnet_request_user), E_TELNET_STE_SUB_GET_USER);
                break;
            case E_TELNET_STE_SUB_GET_USER:
                if (E_TELNET_RESULT_OK == telnet_recv_text_non_blocking(telnet_data.rxBuffer + telnet_data.rxWindex,
                                                                        TELNET_RX_BUFFER_SIZE - telnet_data.rxWindex,
                                                                        &rxLen)) {
                    int result;
                    if ((result = telnet_process_credential (telnet_user, rxLen))) {
                        telnet_data.credentialsValid = result > 0 ? true : false;
                        telnet_data.substate.connected = E_TELNET_STE_SUB_REQ_PASSWORD;
                    }
                }
                break;
            case E_TELNET_STE_SUB_REQ_PASSWORD:
                telnet_send_and_proceed((void *)telnet_request_password, strlen(telnet_request_password), E_TELNET_STE_SUB_SND_PASSWORD_OPTIONS);
                break;
            case E_TELNET_STE_SUB_SND_PASSWORD_OPTIONS:
                // to catch any left over characters from the previous actions
                telnet_recv_text_non_blocking(telnet_data.rxBuffer, TELNET_RX_BUFFER_SIZE, &rxLen);
                telnet_send_and_proceed((void *)telnet_options_pass, sizeof(telnet_options_pass), E_TELNET_STE_SUB_GET_PASSWORD);
                break;
            case E_TELNET_STE_SUB_GET_PASSWORD:
                if (E_TELNET_RESULT_OK == telnet_recv_text_non_blocking(telnet_data.rxBuffer + telnet_data.rxWindex,
                                                                        TELNET_RX_BUFFER_SIZE - telnet_data.rxWindex,
                                                                        &rxLen)) {
                    int result;
                    if ((result = telnet_process_credential (telnet_pass, rxLen))) {
                        if ((telnet_data.credentialsValid = telnet_data.credentialsValid && (result > 0 ? true : false))) {
                            telnet_data.substate.connected = E_TELNET_STE_SUB_SND_REPL_OPTIONS;
                        }
                        else {
                            telnet_data.substate.connected = E_TELNET_STE_SUB_INVALID_LOGIN;
                        }
                    }
                }
                break;
            case E_TELNET_STE_SUB_INVALID_LOGIN:
                if (E_TELNET_RESULT_OK == telnet_send_non_blocking((void *)telnet_invalid_login, strlen(telnet_invalid_login))) {
                    telnet_data.credentialsValid = true;
                    if (++telnet_data.loginRetries >= TELNET_LOGIN_RETRIES_MAX) {
                        _telnet_reset();
                    }
                    else {
                        telnet_data.substate.connected = E_TELNET_STE_SUB_SND_USER_OPTIONS;
                    }
                }
                break;
            case E_TELNET_STE_SUB_SND_REPL_OPTIONS:
                telnet_send_and_proceed((void *)telnet_options_repl, sizeof(telnet_options_repl), E_TELNET_STE_SUB_LOGIN_SUCCESS);
                break;
            case E_TELNET_STE_SUB_LOGIN_SUCCESS:
                if (E_TELNET_RESULT_OK == telnet_send_non_blocking((void *)telnet_login_success, strlen(telnet_login_success))) {
                    // clear the current line and force the prompt
                    telnet_reset_buffer();
                    telnet_data.state= E_TELNET_STE_LOGGED_IN;
                	printf("\n[Telnet] User logged in, REPL disabled.\n         Ctrl-T to terminate connection\n");
                }
                break;
            default:
                break;
            }
            break;
        case E_TELNET_STE_LOGGED_IN:
            telnet_process();
            break;
        default:
            break;
    }

    if (telnet_data.state >= E_TELNET_STE_CONNECTED) {
        if ((telnet_data.timeout + telnet_timeout) < mp_hal_ticks_ms()) {
        	if (telnet_data.state == E_TELNET_STE_LOGGED_IN) printf("[Telnet] Connection timeout, terminated\n");
            _telnet_reset();
        }
    }
    xSemaphoreGive(telnet_mutex);
    return 0;
}

//-----------------------
void telnet_init (void) {
	telnet_stop = 0;
    // Allocate memory for the receive buffer (from the RTOS heap)
	if (telnet_data.rxBuffer) free(telnet_data.rxBuffer);
	memset(&telnet_data, 0, sizeof(telnet_data_t));
    telnet_data.rxBuffer = malloc(TELNET_RX_BUFFER_SIZE);
    telnet_data.state = E_TELNET_STE_DISABLED;
	if (telnet_mutex == NULL) telnet_mutex = xSemaphoreCreateMutex();
}

//-------------------------
void telnet_deinit (void) {
	if (telnet_data.rxBuffer) free(telnet_data.rxBuffer);
	memset(&telnet_data, 0, sizeof(telnet_data_t));
}


// Send string to telnet client
//----------------------------------------------
void telnet_tx_strn (const char *str, int len) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL) || (telnet_data.n_sd <= 0)) return;
    if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return;

    if ((telnet_data.state == E_TELNET_STE_LOGGED_IN) && (len > 0)) {
        int32_t retries = 0;
        uint32_t delay = TELNET_WAIT_TIME_MS;

    	do {
    		if (send(telnet_data.n_sd, str, len, 0) > 0) {
    			telnet_data.timeout = mp_hal_ticks_ms();
    			break;
    		}
    		else if (EAGAIN != errno) break;
    		// start with the default delay and increment it on each retry
    		vTaskDelay(delay++ / portTICK_PERIOD_MS);
    	} while (++retries <= TELNET_TX_RETRIES_MAX);
    }
	xSemaphoreGive(telnet_mutex);
}

// Return true if any character is available in RX buffer
//-------------------------
bool telnet_rx_any (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL) || (telnet_data.n_sd <= 0)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;

	bool res = (telnet_data.n_sd > 0) ? ((telnet_data.rxRindex != telnet_data.rxWindex) && (telnet_data.state == E_TELNET_STE_LOGGED_IN)) : false;
	xSemaphoreGive(telnet_mutex);
	return res;
}

// Return one character from RX buffer if available
//-------------------------
int telnet_rx_char (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL) || (telnet_data.n_sd <= 0)) return -1;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return -1;

    int rx_char = -1;
	if (telnet_data.rxRindex != telnet_data.rxWindex) {
        // rxRindex must be uint8_t and TELNET_RX_BUFFER_SIZE == 256 so that it wraps around automatically
        rx_char = (int)telnet_data.rxBuffer[telnet_data.rxRindex++];
    }
	xSemaphoreGive(telnet_mutex);
    return rx_char;
}

// Return true if client is logged in
//---------------------------
bool telnet_loggedin (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL) || (telnet_data.n_sd <= 0)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;

	bool res = (telnet_data.state == E_TELNET_STE_LOGGED_IN);
	xSemaphoreGive(telnet_mutex);
    return res;
}

// Enable telnet server
//-------------------------
bool telnet_enable (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;
	if (telnet_data.state == E_TELNET_STE_LOGGED_IN) return false;

	telnet_data.enabled = true;
	xSemaphoreGive(telnet_mutex);
	return true;
}

//-----------------------------
bool telnet_isenabled (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;

	bool res = telnet_data.enabled;
	xSemaphoreGive(telnet_mutex);
	return res;
}

// Reset telnet server
//------------------------
bool telnet_reset (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;

	_telnet_reset();
	xSemaphoreGive(telnet_mutex);
	return true;
}

// Disable telnet server
//--------------------------
bool telnet_disable (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;
	if (telnet_data.state == E_TELNET_STE_LOGGED_IN) return false;

	_telnet_reset();
    telnet_data.enabled = false;
    telnet_data.state = E_TELNET_STE_DISABLED;
	xSemaphoreGive(telnet_mutex);
	return true;
}

// Return current telnet server state
//---------------------
int telnet_getstate() {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return -1;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return -1;

	int tstate = telnet_data.state;
	xSemaphoreGive(telnet_mutex);
	return tstate;
}

//----------------------------
bool telnet_terminate (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;
	if (telnet_data.state == E_TELNET_STE_LOGGED_IN) return false;

	telnet_stop = 1;
	xSemaphoreGive(telnet_mutex);
	return true;
}

//----------------------------
bool telnet_stop_requested() {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return false;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return false;

	bool res = (telnet_stop == 1);
	xSemaphoreGive(telnet_mutex);
	return res;
}

//----------------------------------
int32_t telnet_get_maxstack (void) {
	if ((TelnetTaskHandle == NULL) || (telnet_mutex == NULL)) return -1;
	if (xSemaphoreTake(telnet_mutex, TELNET_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return -1;

	int32_t maxstack = telnet_stack_size - uxTaskGetStackHighWaterMark(TelnetTaskHandle);
	xSemaphoreGive(telnet_mutex);
	return maxstack;
}


#endif
