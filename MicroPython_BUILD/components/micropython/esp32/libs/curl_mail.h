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

#ifndef _CURL_MAIL_H
#define _CURL_MAIL_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "sdkconfig.h"

#if defined(CONFIG_MICROPY_USE_CURL)

#define CURLMAIL_PROTOCOL_SMTP		1
#define CURLMAIL_PROTOCOL_SMTPS		2
#define CURLMAIL_PROTOCOL_IMAP      3
#define CURLMAIL_PROTOCOL_IMAPS     4

#define CURLMAIL_MAX_ATTACHMENTS	4


typedef struct email_info_struct* curl_mail;


curl_mail curlmail_create (const char* from, const char* subject);

void curlmail_destroy (curl_mail mail_object);

void curlmail_set_from (curl_mail mail_object, const char* from);

void curlmail_add_to (curl_mail mail_object, const char* mail_addr);

void curlmail_add_cc (curl_mail mail_object, const char* mail_addr);

void curlmail_add_bcc (curl_mail mail_object, const char* mail_addr);

void curlmail_set_subject (curl_mail mail_object, const char* subject);

void curlmail_add_header (curl_mail mail_object, const char* headerline);

void curlmail_set_body (curl_mail mail_object, const char* body);

void curlmail_add_attachment_file(curl_mail mail_object, const char* path, const char* mimetype);

const char* curlmail_protocol_send (curl_mail mail_object, const char* smtpserver, unsigned int smtpport, int protocol, const char* username, const char* password);

#endif

#ifdef __cplusplus
}
#endif

#endif //_CURL_MAIL_H
