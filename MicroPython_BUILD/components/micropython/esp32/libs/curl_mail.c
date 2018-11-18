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

#if defined(CONFIG_MICROPY_USE_CURL)

#include "libs/curl_mail.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <unistd.h>
#include <curl/curl.h>

#include "esp_log.h"
#include "libs/espcurl.h"
#include "py/mpthread.h"

#define CRLF "\r\n"
#define CRLFLENGTH 2

#define MIME_LINE_MAX_WIDTH 992

// Constants defining the email send state
#define SEND_MAIL_STATE_INITIALIZE	0
#define SEND_MAIL_STATE_HEADER		1
#define SEND_MAIL_STATE_BODY		2
#define SEND_MAIL_STATE_BODY_DONE	3
#define SEND_MAIL_STATE_ATTACHMENT	4
#define SEND_MAIL_STATE_END			5
#define SEND_MAIL_STATE_DONE		6

static const char* default_mime_type = "text/plain";
static const char *MAIL_TAG = "[Curl_mail]";
static const char* MEMORY_ALLOCATION_ERROR = "Error allocating memory";
// ---- Base64 Encoding/Decoding Table ---
static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//---------------
struct progress {
  double lastruntime;
  CURL *curl;
};

//-----------------------
struct email_attachment {
	char* filename;
	char* mimetype;
	FILE *handle;
};

//------------------------
struct email_info_struct {
	int		state;
	time_t	timestamp;
	char	*from;
	char	*to;
	char	*cc;
	char	*bcc;
	char	*subject;
	char	*header;
	char	*body;
	struct email_attachment* attachment[CURLMAIL_MAX_ATTACHMENTS];
	int		num_attach;
	char	*buf;
	int		buflen;
	char	*boundary_body;
	char	*boundary_attach;
};


//-----------------------------------------
static char* populate_boundary (char* data)
{
  //replace all 0s in string with random digits
  char* p = data;
  while (*p) {
    if (*p == '0') *p = '0' + rand() % 10;
    p++;
  }
  return data;
}

//---------------------------------------------------------
char* append_to_string (char** in_str, const char* app_str)
{
	//append a string to the end of an existing string
	int len = (*in_str ? strlen(*in_str) : 0);
	char *p = (char*)realloc(*in_str, len + strlen(app_str) + 1);
	if (p == NULL) {
		free(p);
		ESP_LOGE(MAIL_TAG, "%s", MEMORY_ALLOCATION_ERROR);
		return NULL;
	}
	*in_str = p;
	strcpy(*in_str + len, app_str);
	return *in_str;
}

//--------------------------------------------------------------
curl_mail curlmail_create(const char* from, const char* subject)
{
	struct email_info_struct* mail_object;
	if ((mail_object = (struct email_info_struct*)malloc(sizeof(struct email_info_struct))) == NULL) {
		ESP_LOGE(MAIL_TAG, "%s", MEMORY_ALLOCATION_ERROR);
		return NULL;
	}

	mail_object->state = 0;
	mail_object->timestamp = time(NULL);
	mail_object->from = NULL;
	if (from) {
		append_to_string(&mail_object->from, "<");
		append_to_string(&mail_object->from, from);
		append_to_string(&mail_object->from, ">");
	}
	mail_object->to = NULL;
	mail_object->cc = NULL;
	mail_object->bcc = NULL;
	mail_object->subject = (subject ? strdup(subject) : NULL);
	mail_object->header = NULL;
	mail_object->body = NULL;
	for (int n=0; n < CURLMAIL_MAX_ATTACHMENTS; n++) {
		mail_object->attachment[n] = NULL;
	}
	mail_object->num_attach = 0;
	mail_object->buf = NULL;
	mail_object->buflen = 0;
	mail_object->boundary_body = NULL;
	mail_object->boundary_attach = NULL;

	srand(time(NULL));
	return mail_object;
}

//------------------------------------------------------------------
static void curlmail_free_attachment(curl_mail mail_object, int idx)
{
	if (mail_object->attachment[idx]) {
		if (mail_object->attachment[idx]->filename) {
			free(mail_object->attachment[idx]->filename);
			mail_object->attachment[idx]->filename = NULL;
		}
		if (mail_object->attachment[idx]->mimetype) {
			free(mail_object->attachment[idx]->mimetype);
			mail_object->attachment[idx]->mimetype = NULL;
		}
		free(mail_object->attachment[idx]);
		mail_object->attachment[idx] = NULL;
	}
}

//------------------------------------------
void curlmail_destroy(curl_mail mail_object)
{
	if (mail_object->from) free(mail_object->from);
	if (mail_object->to) free(mail_object->to);
	if (mail_object->cc) free(mail_object->cc);
	if (mail_object->bcc) free(mail_object->bcc);
	if (mail_object->subject) free(mail_object->subject);
	if (mail_object->header) free(mail_object->header);
	if (mail_object->body) free(mail_object->body);
	if (mail_object->buf) free(mail_object->buf);
	if (mail_object->boundary_body) free(mail_object->boundary_body);
	if (mail_object->boundary_attach) free(mail_object->boundary_attach);
	for (int n=0; n < mail_object->num_attach; n++) {
		curlmail_free_attachment(mail_object, n);
	}
	free(mail_object);
}

//----------------------------------------------------------------
void curlmail_add_to(curl_mail mail_object, const char* mail_addr)
{
	if ((mail_addr == NULL) || (*mail_addr == '\0')) return;
	if (mail_object->to) {
		append_to_string(&mail_object->to, ", <");
	}
	else {
		append_to_string(&mail_object->to, "<");
	}
	append_to_string(&mail_object->to, mail_addr);
	append_to_string(&mail_object->to, ">");
}

//----------------------------------------------------------------
void curlmail_add_cc(curl_mail mail_object, const char* mail_addr)
{
	if ((mail_addr == NULL) || (*mail_addr == '\0')) return;
	if (mail_object->cc) {
		append_to_string(&mail_object->cc, ", <");
	}
	else {
		append_to_string(&mail_object->cc, "<");
	}
	append_to_string(&mail_object->cc, mail_addr);
	append_to_string(&mail_object->cc, ">");
}

//-----------------------------------------------------------------
void curlmail_add_bcc(curl_mail mail_object, const char* mail_addr)
{
	if ((mail_addr == NULL) || (*mail_addr == '\0')) return;
	if (mail_object->bcc) {
		append_to_string(&mail_object->bcc, ", <");
	}
	else {
		append_to_string(&mail_object->bcc, "<");
	}
	append_to_string(&mail_object->bcc, mail_addr);
	append_to_string(&mail_object->bcc, ">");
}

//-------------------------------------------------------------------
void curlmail_set_subject(curl_mail mail_object, const char* subject)
{
	if (mail_object->subject) free(mail_object->subject);
	mail_object->subject = (subject ? strdup(subject) : NULL);
}

//---------------------------------------------------------------------
void curlmail_add_header(curl_mail mail_object, const char* headerline)
{
  append_to_string(&mail_object->header, headerline);
  append_to_string(&mail_object->header, CRLF);
}

//---------------------------------------------------------------
void curlmail_set_body (curl_mail mail_object, const char* body)
{
	if (mail_object->body) free(mail_object->body);
	mail_object->body = (body ? strdup(body) : NULL);
}

//----------------------------------------------------------------------------------------------
void curlmail_add_attachment_file(curl_mail mail_object, const char* path, const char* mimetype)
{
	if (mail_object->num_attach >= CURLMAIL_MAX_ATTACHMENTS) return;

	mail_object->attachment[mail_object->num_attach] = (struct email_attachment*)malloc(sizeof(struct email_attachment));
	if (mail_object->attachment[mail_object->num_attach]) {
		mail_object->attachment[mail_object->num_attach]->handle = NULL;

		mail_object->attachment[mail_object->num_attach]->filename = malloc(strlen(path)+1);
		if (mail_object->attachment[mail_object->num_attach]->filename == NULL) {
			free(mail_object->attachment[mail_object->num_attach]);
			return;
		}
		strcpy(mail_object->attachment[mail_object->num_attach]->filename, path);
		if (mimetype) {
			mail_object->attachment[mail_object->num_attach]->mimetype = malloc(strlen(mimetype)+1);
			if (mail_object->attachment[mail_object->num_attach]->mimetype == NULL) {
				free(mail_object->attachment[mail_object->num_attach]->filename);
				free(mail_object->attachment[mail_object->num_attach]);
				return;
			}
			strcpy(mail_object->attachment[mail_object->num_attach]->mimetype, mimetype);
		}
		else mail_object->attachment[mail_object->num_attach]->mimetype = NULL;
		mail_object->num_attach++;
	}
}

//-------------------------------------------------
static const char *file_base_name(const char *path)
{
	// get base filename
	const char* basename = path + strlen(path);
	while (basename != path) {
		basename--;
		if (*basename == '/') {
			basename++;
			break;
		}
	}
	return basename;
}

//--------------------------------------------------------------------------------
static size_t curlmail_get_data(void* ptr, size_t size, size_t nmemb, void* userp)
{
	struct email_info_struct* mail_object = (struct email_info_struct*)userp;

	// Exit if no data is requested
	if (size * nmemb == 0)
	return 0;

	// Do some initialization on first run
	if (mail_object->state == SEND_MAIL_STATE_INITIALIZE) {
		if (mail_object->buf) free(mail_object->buf);
		mail_object->buf = NULL;
		mail_object->buflen = 0;
		if (mail_object->boundary_body) free(mail_object->boundary_body);
		mail_object->boundary_body = NULL;
		if (mail_object->boundary_attach) free(mail_object->boundary_attach);
		mail_object->boundary_attach = NULL;
		mail_object->state++; // -> SEND_MAIL_STATE_HEADER
	}

	// Check if partial data is pending
	while (mail_object->buflen == 0) {
		// ** No partial data is pending, process current part of mail
		//-------------------------------------------------------------------------------
		if (mail_object->buflen == 0 && mail_object->state == SEND_MAIL_STATE_HEADER) {
			// === HEADER, generate header part ===
			char** p = &mail_object->buf;
			append_to_string(p, "User-Agent: MicroPython_ESP32_mail v1.0" CRLF);
			if (mail_object->timestamp != 0) {
				char timestamptext[32];
				//format timestamp
				if (strftime(timestamptext, sizeof(timestamptext), "%a, %d %b %Y %H:%M:%S %z", localtime(&mail_object->timestamp))) {
					append_to_string(p, "Date: ");
					append_to_string(p, timestamptext);
					append_to_string(p, CRLF);
				}
			}
			if (mail_object->from && *mail_object->from) {
				append_to_string(p, "From: ");
				append_to_string(p, mail_object->from);
				append_to_string(p, CRLF);
			}
			if (mail_object->to && *mail_object->to) {
				append_to_string(p, "To: ");
				append_to_string(p, mail_object->to);
				append_to_string(p, CRLF);
			}
			if (mail_object->cc && *mail_object->cc) {
				append_to_string(p, "Cc: ");
				append_to_string(p, mail_object->cc);
				append_to_string(p, CRLF);
			}
			if (mail_object->bcc && *mail_object->bcc) {
				append_to_string(p, "Bcc: ");
				append_to_string(p, mail_object->bcc);
				append_to_string(p, CRLF);
			}
			if (mail_object->subject) {
				append_to_string(p, "Subject: ");
				append_to_string(p, mail_object->subject);
				append_to_string(p, CRLF);
			}
			if (mail_object->header) {
				append_to_string(p, mail_object->header);
			}
			if (mail_object->num_attach > 0) {
				append_to_string(p, "MIME-Version: 1.0" CRLF);
				mail_object->boundary_attach = populate_boundary(strdup("=PART_SEPARATOR_0000_0000_0000_0000_0000_0000"));
				append_to_string(p, "Content-Type: multipart/mixed; boundary=\"");
				append_to_string(p, mail_object->boundary_attach);
				append_to_string(p, "\"" CRLF CRLF "This is a multipart message in MIME format." CRLF CRLF "--");
				append_to_string(p, mail_object->boundary_attach);
				append_to_string(p, CRLF);
			}
			if (mail_object->body) {
				mail_object->boundary_body = populate_boundary(strdup("=BODY_SEPARATOR_0000_0000_0000_0000_0000_0000"));
				append_to_string(p, "Content-Type: multipart/alternative; boundary=\"");
				append_to_string(p, mail_object->boundary_body);
				append_to_string(p, "\"" CRLF);
			}
			mail_object->buflen = (mail_object->buf ? strlen(mail_object->buf) : 0);
			mail_object->state++; // -> SEND_MAIL_STATE_BODY
		}

		//-----------------------------------------------------------------------------
		if (mail_object->buflen == 0 && mail_object->state == SEND_MAIL_STATE_BODY) {
			if (mail_object->body) {
				// === BODY ===
				// generate body header
				if (mail_object->boundary_body) {
					mail_object->buf = append_to_string(&mail_object->buf, CRLF "--");
					mail_object->buf = append_to_string(&mail_object->buf, mail_object->boundary_body);
					mail_object->buf = append_to_string(&mail_object->buf, CRLF);
				}
				mail_object->buf = append_to_string(&mail_object->buf, "Content-Type: ");
				mail_object->buf = append_to_string(&mail_object->buf, default_mime_type);
				mail_object->buf = append_to_string(&mail_object->buf, CRLF "Content-Transfer-Encoding: 8bit" CRLF "Content-Disposition: inline" CRLF CRLF);
				// append body
				mail_object->buf = append_to_string(&mail_object->buf, mail_object->body);
				mail_object->buflen = (mail_object->buf ? strlen(mail_object->buf) : 0);
			}
			mail_object->state++; // --> SEND_MAIL_STATE_BODY_DONE
		}

		//----------------------------------------------------------------------------------
		if (mail_object->buflen == 0 && mail_object->state == SEND_MAIL_STATE_BODY_DONE) {
			if (mail_object->body) {
				// === BODY DONE ===
				mail_object->buf = NULL;
				if (mail_object->boundary_body) {
					mail_object->buf = append_to_string(&mail_object->buf, CRLF "--");
					mail_object->buf = append_to_string(&mail_object->buf, mail_object->boundary_body);
					mail_object->buf = append_to_string(&mail_object->buf, "--" CRLF);
					mail_object->buflen = strlen(mail_object->buf);
					free(mail_object->boundary_body);
					mail_object->boundary_body = NULL;
				}
			}
			mail_object->state++; // -> SEND_MAIL_STATE_ATTACHMENT
		}

		//-----------------------------------------------------------------------------------
		if (mail_object->buflen == 0 && mail_object->state == SEND_MAIL_STATE_ATTACHMENT) {
			// === ATTACHMENT ===
			if ((mail_object->num_attach > 0) && (mail_object->attachment[mail_object->num_attach-1])) {
				if (!mail_object->attachment[mail_object->num_attach-1]->handle) {
					//open file to attach
					mail_object->attachment[mail_object->num_attach-1]->handle = fopen(mail_object->attachment[mail_object->num_attach-1]->filename, "rb");
					if (mail_object->attachment[mail_object->num_attach-1]->handle) {
						//generate attachment header
						mail_object->buf = NULL;
						if (mail_object->boundary_attach) {
							mail_object->buf = append_to_string(&mail_object->buf, CRLF "--");
							mail_object->buf = append_to_string(&mail_object->buf, mail_object->boundary_attach);
							mail_object->buf = append_to_string(&mail_object->buf, CRLF);
						}
						mail_object->buf = append_to_string(&mail_object->buf, "Content-Type: ");
						mail_object->buf = append_to_string(&mail_object->buf,
								(mail_object->attachment[mail_object->num_attach-1]->mimetype ? mail_object->attachment[mail_object->num_attach-1]->mimetype : "application/octet-stream"));
						mail_object->buf = append_to_string(&mail_object->buf, "; Name=\"");
						mail_object->buf = append_to_string(&mail_object->buf,
								(mail_object->attachment[mail_object->num_attach-1]->filename ? file_base_name(mail_object->attachment[mail_object->num_attach-1]->filename) : "ATTACHMENT"));
						mail_object->buf = append_to_string(&mail_object->buf, "\"" CRLF "Content-Disposition: attachment; filename=\"");
						mail_object->buf = append_to_string(&mail_object->buf,
								(mail_object->attachment[mail_object->num_attach-1]->filename ? file_base_name(mail_object->attachment[mail_object->num_attach-1]->filename) : "ATTACHMENT"));
						mail_object->buf = append_to_string(&mail_object->buf, "\"" CRLF "Content-Transfer-Encoding: base64" CRLF CRLF);
						mail_object->buflen = strlen(mail_object->buf);
					}
				}
				else {
					//generate next line of attachment data
					size_t n = 0, file_data = 0;
					int mimelinepos = 0;
					unsigned char igroup[3] = {0, 0, 0};
					unsigned char ogroup[4];
					mail_object->buflen = 0;
					if ((mail_object->buf = (char*)malloc(MIME_LINE_MAX_WIDTH + CRLFLENGTH + 1)) == NULL) {
						ESP_LOGE(MAIL_TAG, "%s", MEMORY_ALLOCATION_ERROR);
						n = 0;
					}
					else {
						while ((mimelinepos < MIME_LINE_MAX_WIDTH) && (n = fread(igroup, 1, 3, mail_object->attachment[mail_object->num_attach-1]->handle))) {
							// Encode data to base64
							ogroup[0] = base64[igroup[0] >> 2];
							ogroup[1] = base64[((igroup[0] & 3) << 4) | (igroup[1] >> 4)];
							ogroup[2] = base64[((igroup[1] & 0xF) << 2) | (igroup[2] >> 6)];
							ogroup[3] = base64[igroup[2] & 0x3F];
							file_data += n;
							//pad with "=" characters if less than 3 characters were read
							if (n < 3) {
								ogroup[3] = '=';
								if (n < 2) ogroup[2] = '=';
							}
							memcpy(mail_object->buf + mimelinepos, ogroup, 4);
							mail_object->buflen += 4;
							mimelinepos += 4;
						}
						if (mimelinepos > 0) {
							memcpy(mail_object->buf + mimelinepos, CRLF, CRLFLENGTH);
							mail_object->buflen += CRLFLENGTH;
						}
					}
					if (n <= 0) {
						//end of file
						fclose(mail_object->attachment[mail_object->num_attach-1]->handle);
						mail_object->attachment[mail_object->num_attach-1]->handle = NULL;
						mail_object->state++; // -> SEND_MAIL_STATE_END
					}
				}
			}
			else mail_object->state = SEND_MAIL_STATE_DONE;
		}

		//----------------------------------------------------------------------------
		if (mail_object->buflen == 0 && mail_object->state == SEND_MAIL_STATE_END) {
			if ((mail_object->num_attach > 0) && (mail_object->attachment[mail_object->num_attach-1])) {
				mail_object->buf = NULL;
				mail_object->buflen = 0;
				curlmail_free_attachment(mail_object, mail_object->num_attach-1);
				mail_object->num_attach--;
				if (mail_object->num_attach > 0) {
					// Send the next attachment
					mail_object->state--; // -> SEND_MAIL_STATE_ATTACHMENT
				}
				else {
					if (mail_object->boundary_attach) {
						// The last attachment was sent
						mail_object->buf = append_to_string(&mail_object->buf, CRLF "--");
						mail_object->buf = append_to_string(&mail_object->buf, mail_object->boundary_attach);
						mail_object->buf = append_to_string(&mail_object->buf, "--" CRLF);
						mail_object->buflen = strlen(mail_object->buf);
					}
					free(mail_object->boundary_attach);
					mail_object->boundary_attach = NULL;
					mail_object->state++; // -> SEND_MAIL_STATE_DONE
				}
			}
			else mail_object->state = SEND_MAIL_STATE_DONE;
		}

		if (mail_object->buflen == 0 && mail_object->state == SEND_MAIL_STATE_DONE) {
			break;
		}
	} // while (mail_object->buflen == 0)


	// -----------------------------------------------
	// Send any pending data from mail object's buffer
	// -----------------------------------------------
	if (mail_object->buflen > 0) {
		int len = ((mail_object->buflen > (size * nmemb)) ? size * nmemb : mail_object->buflen);
		memcpy(ptr, mail_object->buf, len);
		if (len < mail_object->buflen) {
			// some data are still in buffer
			mail_object->buf = memmove(mail_object->buf, mail_object->buf + len, mail_object->buflen - len);
			mail_object->buflen -= len;
		}
		else {
			// Add data from buffer are sent, free the buffer
			free(mail_object->buf);
			mail_object->buf = NULL;
			mail_object->buflen = 0;
		}
		return len;
	}

	//if (mail_object->state != SEND_MAIL_STATE_DONE)
	// ** this should never be reached
	mail_object->state = 0;
	return 0;
}

//------------------------------------------------------------------------------------------------------
static int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	struct progress *myp = (struct progress *)p;
	CURL *curl = myp->curl;
	double curtime = 0;

	curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &curtime);

	/* under certain circumstances it may be desirable for certain functionality
	to only run every N seconds, in order to do this the transaction time can
	be used */
	if ((curl_verbose) && ((curtime - myp->lastruntime) >= curl_progress)) {
		if (myp->lastruntime == 0) mp_printf(&mp_plat_print, "\n");
		myp->lastruntime = curtime;
		mp_printf(&mp_plat_print, "TIME: %0.1f UP: %" CURL_FORMAT_CURL_OFF_T"\r", curtime, ulnow);
	}
	return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------------------------------------
const char* curlmail_protocol_send(curl_mail mail_object, const char* smtpserver, unsigned int smtpport, int protocol, const char* username, const char* password)
{
	// Curl based send mail
	CURL *curl;
	CURLcode result = CURLE_FAILED_INIT;
	struct progress prog;

	curl = curl_easy_init();
	if (curl == NULL) return curl_easy_strerror(result);

	prog.lastruntime = 0;
	prog.curl = curl;

	struct curl_slist *recipients = NULL;

	//set destination URL
	char* addr;
	size_t len = strlen(smtpserver) + 14;
	if ((addr = (char*)malloc(len)) == NULL) {
		ESP_LOGE(MAIL_TAG, "Error allocating memory");
		return MEMORY_ALLOCATION_ERROR;
	}
	snprintf(addr, len, "%s://%s:%u", (protocol == CURLMAIL_PROTOCOL_SMTPS ? "smtps" : "smtp"), smtpserver, smtpport);
	curl_easy_setopt(curl, CURLOPT_URL, addr);
	free(addr);

	// Try using Transport Layer Security (TLS), but continue anyway if it fails
	curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_TRY);
	//don't fail if the TLS/SSL a certificate could not be verified
	//alternative: add the issuer certificate (or the host certificate if
	//the certificate is self-signed) to the set of certificates that are
	//known to libcurl using CURLOPT_CAINFO and/or CURLOPT_CAPATH
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

	//set authentication credentials if provided
	if (username && *username) curl_easy_setopt(curl, CURLOPT_USERNAME, username);
	if (password) curl_easy_setopt(curl, CURLOPT_PASSWORD, password);

	//set from value for envelope reverse-path
	if (mail_object->from && *mail_object->from) curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail_object->from);

	//set recipient
	recipients = curl_slist_append(recipients, mail_object->to);

	if (mail_object->cc) recipients = curl_slist_append(recipients, mail_object->cc);
	if (mail_object->bcc) recipients = curl_slist_append(recipients, mail_object->bcc);

	curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
	//set callback function for getting message body
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, curlmail_get_data);
	curl_easy_setopt(curl, CURLOPT_READDATA, mail_object);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L); //set CURLOPT_UPLOAD to 1 to not use VRFY and other unneeded commands

	//enable debugging if requested
	curl_easy_setopt(curl, CURLOPT_VERBOSE, curl_verbose);

	curl_easy_setopt(curl, CURLOPT_TIMEOUT, curl_timeout);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);

	// pass the struct pointer into the xferinfo function, note that this is an alias to CURLOPT_PROGRESSDATA
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);
	if (curl_progress) curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);

	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 2048);

	//send the message
	MP_THREAD_GIL_EXIT();
	result = curl_easy_perform(curl);
	MP_THREAD_GIL_ENTER();

	if (curl_verbose) mp_printf(&mp_plat_print, "\n");
	//free the list of recipients and clean up
	curl_slist_free_all(recipients);
	curl_easy_cleanup(curl);

	return (result == CURLE_OK ? NULL : curl_easy_strerror(result));
}

#endif
