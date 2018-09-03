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


#ifndef _ESPCURL_H_
#define _ESPCURL_H_

#include "sdkconfig.h"

#if defined(CONFIG_MICROPY_USE_CURL) || defined(CONFIG_MICROPY_USE_SSH)

#include <stdint.h>

#define MIN_HDR_BUF_LEN		128
#define MIN_BODY_BUF_LEN	256

// ================
// Public functions
// ================

#ifdef CONFIG_MICROPY_USE_CURL

#include "curl/curl.h"

#define GMAIL_SMTP			"smtp.gmail.com"
#define GMAIL_PORT			465
#define GMAIL_IMAP          "imap.gmail.com"
#define GMAIL_IMAP_PORT     993

struct curl_httppost *formpost;
struct curl_httppost *lastptr;

// Some configuration variables
extern uint8_t curl_verbose;   // show detailed info of what curl functions are doing
extern uint8_t curl_progress;  // show progress during transfers
extern uint16_t curl_timeout;  // curl operations timeout in seconds
extern uint32_t curl_maxbytes; // limit download length
extern uint8_t curl_initialized;
extern uint8_t curl_nodecode;

/*
 * ----------------------------------------------------------
 * int res = Curl_GET(url, fname, hdr, body, hdrlen, bodylen)
 * ----------------------------------------------------------
 *
 * GET data from http or https server
 *
 * Params:
 * 		   url:	pointer to server url, if starting with 'https://' SSL will be used
 * 		 fname:	pointer to file name; if not NULL response body will be written to the file of that name
 * 		   hdr:	pointer to char buffer to which the received response header or error message will be written
 * 		  body:	pointer to char buffer to which the received response body or error message will be written
 *      hdrlen: length of the hdr buffer, must be greather than MIN_HDR_BODY_BUF_LEN
 *     bodylen: length of the body buffer, must be greather than MIN_HDR_BODY_BUF_LEN
 *
 * Returns:
 * 		 res:	0 success, error code on error
 *
 */
//===================================================================================
int Curl_GET(char *url, char *fname, char *hdr, char *body, int hdrlen, int bodylen);


//==========================================================================================================================================================================================================
int Curl_IMAP_GET(const char *opts, char *fname, char *hdr, char *body, int hdrlen, int bodylen, const char* imapserver, unsigned int imapport, const char* username, const char* password, char *cust_req);


/*
 * -----------------------------------------------------
 * int res = Curl_POST(url, hdr, body, hdrlen, bodylen);
 * -----------------------------------------------------
 *
 * POST data to http or https server
 *
 * Params:
 * 		   url:	pointer to server url, if starting with 'https://' SSL will be used
 * 		   hdr:	pointer to char buffer to which the received response header or error message will be written
 * 		  body:	pointer to char buffer to which the received response body or error message will be written
 *      hdrlen: length of the hdr buffer, must be greather than MIN_HDR_BODY_BUF_LEN
 *     bodylen: length of the body buffer, must be greather than MIN_HDR_BODY_BUF_LEN
 *
 * Returns:
 * 		 res:	0 success, error code on error
 * 
 * NOTE:
 *      Before calling this function, POST data has to be set using curl_formadd() function
 *      If adding the text parameter use the format:
 *          curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "key_string", CURLFORM_COPYCONTENTS, "value_string", CURLFORM_END);
 *      If adding the file, use the format:
 *          curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "file_key_string", CURLFORM_FILE, "file_name", CURLFORM_END);
 * 
 */
//=======================================================================
int Curl_POST(char *url, char *hdr, char *body, int hdrlen, int bodylen);

#ifdef CONFIG_MICROPY_USE_CURLFTP

/*
 * ------------------------------------------------------------------------------
 * int res = Curl_FTP(upload, url, user_pass, fname, hdr, body, hdrlen, bodylen);
 * ------------------------------------------------------------------------------
 *
 * FTP operations; LIST, GET file, PUT file
 *   if the server supports SSL/TLS, secure transport will be used for login and data transfer
 *
 * Params:
 * 		   url:	pointer to server url, if starting with 'https://' SSL will be used
 * 	 user_pass:	pointer to user name and password in the format "user:password"
 * 		   hdr:	pointer to char buffer to which the received response header or error message will be written
 * 		  body:	pointer to char buffer to which the received response body or error message will be written
 *      hdrlen: length of the hdr buffer, must be greather than MIN_HDR_BODY_BUF_LEN
 *     bodylen: length of the body buffer, must be greather than MIN_HDR_BODY_BUF_LEN
 * 		 fname: pointer to the file name
 *              IF NOT NULL && upload=0, LIST or file will be written to the file of that name
 *              IF upload=1	file of that name will be PUT on the server
 *
 * Returns:
 * 		 res:	0 success, error code on error
 *
 */
//====================================================================================================================
int Curl_FTP(uint8_t upload, char *url, char *user_pass, char *fname, char *hdr, char *body, int hdrlen, int bodylen);

#endif

#endif //CONFIG_MICROPY_USE_CURL

//==================
void Curl_cleanup();

#ifdef CONFIG_MICROPY_USE_SSH

extern uint8_t ssh2_verbose;
extern uint8_t ssh2_progress;
extern uint16_t ssh2_session_trace;
extern uint8_t ssh2_session_timeout;
extern int ssh2_hdr_maxlen;
extern int ssh2_body_maxlen;
extern uint32_t ssh2_maxbytes;

//==================================================================================================================================================================
int ssh_SCP(uint8_t type, char *server, char *port, char * scppath, char *user, char *pass, char *key, char *fname, char *hdr, char *body, int hdrlen, int bodylen);

#endif  // CONFIG_MICROPY_USE_SSH


//================================
char *url_encode(const char *str);

#endif // defined(CONFIG_MICROPY_USE_CURL) || defined(CONFIG_MICROPY_USE_SSH)

#endif
