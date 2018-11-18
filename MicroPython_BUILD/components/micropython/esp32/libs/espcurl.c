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

#if defined(CONFIG_MICROPY_USE_CURL) || defined(CONFIG_MICROPY_USE_SSH)

#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "libs/espcurl.h"
#include "libs/libGSM.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "mphalport.h"

#include "esp_wifi_types.h"
#include "tcpip_adapter.h"

#include "modnetwork.h"
#include "modmachine.h"
#include "py/mpthread.h"
#include "py/nlr.h"

#ifdef CONFIG_MICROPY_USE_CURL

const char *CURL_TAG = "[Curl]";

// Set default values for configuration variables
uint8_t curl_verbose = 0;			// show detailed info of what curl functions are doing
uint8_t curl_progress = 0;			// show progress during curl transfers
uint16_t curl_timeout = 60;			// curl operations timeout in seconds
uint32_t curl_maxbytes = 300000;	// limit download length
uint8_t curl_initialized = 0;
uint8_t curl_nodecode = 0;			// if set to 1, do not use compression in http transfers

static uint8_t curl_sim_fs = 0;

struct curl_Transfer {
	char *ptr;
	uint32_t len;
	uint32_t size;
	int status;
	uint8_t tofile;
	uint32_t maxlen;
	double lastruntime;
	FILE *file;
	CURL *curl;
};

struct curl_httppost *formpost = NULL;
struct curl_httppost *lastptr = NULL;


// Initialize the structure used in curlWrite callback
//-----------------------------------------------------------------------------------------------------------
static void init_curl_Transfer(CURL *curl, struct curl_Transfer *s, char *buf, uint16_t maxlen, FILE* file) {
    s->len = 0;
    s->size = 0;
    s->status = 0;
    s->maxlen = maxlen;
    s->lastruntime = 0;
    s->tofile = 0;
    s->file = file;
    s->ptr = buf;
    s->curl = curl;
    if (s->ptr) s->ptr[0] = '\0';
}

// Callback: Get response header or body to buffer or file
//--------------------------------------------------------------------------------
static size_t curlWrite(void *buffer, size_t size, size_t nmemb, void *userdata) {
	struct curl_Transfer *s = (struct curl_Transfer *)userdata;
	CURL *curl = s->curl;
	double curtime = 0;
	curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &curtime);

	mp_hal_reset_wdt();

	if (!s->tofile) {
		// === Downloading to buffer ===
		char *buf = (char *)buffer;
		if (s->ptr) {
			if ((s->status == 0) && ((size*nmemb) > 0)) {
				for (int i=0; i<(size*nmemb); i++) {
					if (s->len < (s->maxlen-2)) {
						if ((buf[i] == 0x0a) || (buf[i] == 0x0d) || (buf[i] == 0x09) || (buf[i] >= 0x20)) s->ptr[s->len] = buf[i];
						else s->ptr[s->len] = '.';
						s->len++;
						s->ptr[s->len] = '\0';
					}
					else {
						s->status = 1;
						break;
					}
				}
				if ((curl_progress) && ((curtime - s->lastruntime) > curl_progress)) {
					s->lastruntime = curtime;
					ESP_LOGI(CURL_TAG, "* Download: received %d", s->len);
				}
			}
		}
		return size*nmemb;
	}
	else {
		// === Downloading to file ===
		size_t nwrite;

		if (curl_sim_fs) nwrite = size*nmemb;
		else {
			nwrite = fwrite(buffer, 1, size*nmemb, s->file);
			if (nwrite <= 0) {
				ESP_LOGE(CURL_TAG, "* Download: Error writing to file %d", nwrite);
				return 0;
			}
		}

		s->len += nwrite;
		if ((curl_progress) && ((curtime - s->lastruntime) > curl_progress)) {
			s->lastruntime = curtime;
			ESP_LOGI(CURL_TAG, "* Download: received %d", s->len);
		}

		return nwrite;
	}
}

// Callback: ftp PUT file
//--------------------------------------------------------------------------
static size_t curlRead(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct curl_Transfer *s = (struct curl_Transfer *)userdata;
	CURL *curl = s->curl;
	double curtime = 0;
	curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &curtime);
	size_t nread;

	mp_hal_reset_wdt();

	if (curl_sim_fs) {
		if (s->len < 1024) {
			size_t reqsize = size*nmemb;
			nread = 0;
			while ((nread+23) < reqsize) {
				sprintf((char *)(ptr+nread), "%s", "Simulate upload data\r\n");
				nread += 22;
			}
		}
		else nread = 0;
	}
	else {
		nread = fread(ptr, 1, size*nmemb, s->file);
	}

	s->len += nread;
	if ((curl_progress) && ((curtime - s->lastruntime) > curl_progress)) {
		s->lastruntime = curtime;
		ESP_LOGI(CURL_TAG, "* Upload: sent %d", s->len);
	}

	//ESP_LOGI(CURL_TAG, "**Upload, read %d (%d,%d)", nread,size,nmemb);
	if (nread <= 0) return 0;

	return nread;
}

/*
//------------------------------------------------------------------
static int closesocket_callback(void *clientp, curl_socket_t item) {
    int ret = lwip_close(item);
    return ret;
}

//------------------------------------------------------------------------------------------------------------
static curl_socket_t opensocket_callback(void *clientp, curlsocktype purpose, struct curl_sockaddr *address) {
    int s = lwip_socket(address->family, address->socktype, address->protocol);
    return s;
}
*/

// Set some common curl options
//----------------------------------------------
static void _set_default_options(CURL *handle) {
	curl_easy_setopt(handle, CURLOPT_VERBOSE, curl_verbose);

	// ** Set SSL Options
	curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(handle, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(handle, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
	//curl_easy_setopt(handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

	// ==== Provide CA Certs from different file than default ====
	//curl_easy_setopt(handle, CURLOPT_CAINFO, "ca-bundle.crt");
	// ===========================================================

	/*
	curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER , 1L);
	curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST , 1L);
	*/

	/* If the server is redirected, we tell libcurl if we want to follow redirection
	 * There are some problems when redirecting ssl requests, so for now we disable redirection
	 */
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);  // set to 1 to enable redirection
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 3L);

    curl_easy_setopt(handle, CURLOPT_TIMEOUT, (long)curl_timeout);

    curl_easy_setopt(handle, CURLOPT_MAXFILESIZE, (long)curl_maxbytes);
    curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 1L);

    if (curl_nodecode) {
        // Do not decode/decompress received content
        curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "identity");
        curl_easy_setopt(handle, CURLOPT_HTTP_CONTENT_DECODING, 0L);
    }
    else {
        curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(handle, CURLOPT_HTTP_CONTENT_DECODING, 1L);
    }

    //curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1024L);	//bytes/sec
	//curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 4);		//seconds

    // Open&Close socket callbacks can be set if special handling is needed
    /*
    curl_easy_setopt(handle, CURLOPT_CLOSESOCKETFUNCTION, closesocket_callback);
    curl_easy_setopt(handle, CURLOPT_CLOSESOCKETDATA, NULL);
    curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, opensocket_callback);
    curl_easy_setopt(handle, CURLOPT_OPENSOCKETDATA, NULL);
    */
}

//==================================================================================
int Curl_GET(char *url, char *fname, char *hdr, char *body, int hdrlen, int bodylen)
{
	CURL *curl = NULL;
	CURLcode res = 0;
	FILE* file = NULL;
    int err = 0;

    if ((hdr) && (hdrlen < MIN_HDR_BUF_LEN)) {
        err = -1;
        goto exit;
    }
    if ((body) && (bodylen < MIN_BODY_BUF_LEN)) {
        err = -2;
        goto exit;
    }

    struct curl_Transfer get_data;
    struct curl_Transfer get_header;

	if (!url) {
        err = -3;
        goto exit;
    }

	if (!curl_initialized) {
		res = curl_global_init(CURL_GLOBAL_DEFAULT);
		if (res) {
            err = -4;
            goto exit;
        }
		curl_initialized = 1;
	}

	// Create a curl curl
	curl = curl_easy_init();
	if (curl == NULL) {
        err = -5;
        goto exit;
	}

    init_curl_Transfer(curl, &get_data, body, bodylen, NULL);
    init_curl_Transfer(curl, &get_header, hdr, hdrlen, NULL);

	if (fname) {
		if (strcmp(fname, "simulate") == 0) {
			get_data.tofile = 1;
			curl_sim_fs = 1;
		}
		else {
			file = fopen(fname, "wb");
			if (file == NULL) {
				err = -6;
				goto exit;
			}
			get_data.file = file;
			get_data.tofile = 1;
			curl_sim_fs = 0;
		}
	}

    curl_easy_setopt(curl, CURLOPT_URL, url);

    _set_default_options(curl);

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &get_header);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &get_data);

	// Perform the request, res will get the return code
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
    	if (curl_verbose) {
    		ESP_LOGE(CURL_TAG, "curl_easy_perform failed: %s", curl_easy_strerror(res));
    	}
		if (body) snprintf(body, bodylen, "%s", curl_easy_strerror(res));
        err = -7;
        goto exit;
    }

    if (get_data.tofile) {
    	if (curl_progress) {
			double curtime = 0;
			curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &curtime);
			ESP_LOGI(CURL_TAG, "* Download: received %d B; time=%0.1f s; speed=%0.1f KB/sec", get_data.len, curtime, (float)(((get_data.len*10)/curtime) / 10240.0));
    	}
		if (body) {
			if (strcmp(fname, "simulate") == 0) snprintf(body, bodylen, "SIMULATED save to file; size=%d", get_data.len);
			else snprintf(body, bodylen, "Saved to file %s, size=%d", fname, get_data.len);
		}
    }

exit:
	// Cleanup
    if (file) fclose(file);
    if (curl) curl_easy_cleanup(curl);

    return err;
}

// Converts an integer value to its hex character
//-----------------------------
static char to_hex(char code) {
    static char hex[] = "0123456789abcdef";
    return hex[code & 15];
}

//------------------------------------------------------------
/* Returns a url encoded version of str
 * IMPORTANT: be sure to free() the returned string after use
 */
//=================================
char *url_encode(const char *str) {
    const char *pstr = str;
    char *buf = malloc(strlen(str) * 3 + 1);
    char *pbuf = buf;
    while (*pstr) {
        if (*pstr == ' ') {
            *pbuf++ = '%';
            *pbuf++ = to_hex(*pstr >> 4);
            *pbuf++ = to_hex(*pstr & 15);
        }
        else {
            *pbuf++ = *pstr;
        }
        pstr++;
    }
    *pbuf = '\0';
    return buf;
}

//=========================================================================================================================================================================================================
int Curl_IMAP_GET(const char *opts, char *fname, char *hdr, char *body, int hdrlen, int bodylen, const char* imapserver, unsigned int imapport, const char* username, const char* password, char *cust_req)
{
    CURL *curl = NULL;
    CURLcode res = 0;
    FILE* file = NULL;
    int err = 0;

    if ((hdr) && (hdrlen < MIN_HDR_BUF_LEN)) {
        err = -1;
        goto exit;
    }
    if ((body) && (bodylen < MIN_BODY_BUF_LEN)) {
        err = -2;
        goto exit;
    }

    struct curl_Transfer get_data;
    struct curl_Transfer get_header;

    if (!curl_initialized) {
        res = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (res) {
            err = -3;
            goto exit;
        }
        curl_initialized = 1;
    }

    // Create a curl curl
    curl = curl_easy_init();
    if (curl == NULL) {
        err = -4;
        goto exit;
    }

    init_curl_Transfer(curl, &get_data, body, bodylen, NULL);
    init_curl_Transfer(curl, &get_header, hdr, hdrlen, NULL);

    if (fname) {
        if (strcmp(fname, "simulate") == 0) {
            get_data.tofile = 1;
            curl_sim_fs = 1;
        }
        else {
            file = fopen(fname, "wb");
            if (file == NULL) {
                err = -5;
                goto exit;
            }
            get_data.file = file;
            get_data.tofile = 1;
            curl_sim_fs = 0;
        }
    }

    //set destination URL
    char* url;
    char *opts_out = url_encode(opts);
    int opts_len = strlen(opts_out);
    size_t len = strlen(imapserver) + opts_len + 16;
    if ((url = (char*)malloc(len)) == NULL) {
        free(opts_out);
        ESP_LOGE(CURL_TAG, "Error allocating memory");
        err = -6;
        goto exit;
    }

    sprintf(url, "imaps://%s:%u/%s", imapserver, imapport, opts_out);
    free(opts_out);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    printf("=== URL=[%s]\n", url);
    free(url);

    _set_default_options(curl);

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &get_header);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &get_data);

    // Try using Transport Layer Security (TLS), but continue anyway if it fails
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

    //set authentication credentials if provided
    if (username && *username) curl_easy_setopt(curl, CURLOPT_USERNAME, username);
    if (password) curl_easy_setopt(curl, CURLOPT_PASSWORD, password);

    if (cust_req) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, cust_req);
    }
    // Only allow IMAPS, we do not want this to work when unencrypted.
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_IMAPS);

    // Perform the request, res will get the return code
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        if (curl_verbose) {
            ESP_LOGE(CURL_TAG, "curl_easy_perform failed: %s", curl_easy_strerror(res));
        }
        if (body) snprintf(body, bodylen, "%s", curl_easy_strerror(res));
        err = -7;
        goto exit;
    }

    if (get_data.tofile) {
        if (curl_progress) {
            double curtime = 0;
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &curtime);
            ESP_LOGI(CURL_TAG, "* Download: received %d B; time=%0.1f s; speed=%0.1f KB/sec", get_data.len, curtime, (float)(((get_data.len*10)/curtime) / 10240.0));
        }
        if (body) {
            if (strcmp(fname, "simulate") == 0) snprintf(body, bodylen, "SIMULATED save to file; size=%d", get_data.len);
            else snprintf(body, bodylen, "Saved to file %s, size=%d", fname, get_data.len);
        }
    }

exit:
    // Cleanup
    if (file) fclose(file);
    if (curl) curl_easy_cleanup(curl);

    return err;
}

//=======================================================================
int Curl_POST(char *url , char *hdr, char *body, int hdrlen, int bodylen)
{
	CURL *curl = NULL;
	CURLcode res = 0;
    int err = 0;
	struct curl_slist *headerlist=NULL;
	const char hl_buf[] = "Expect:";

    if ((hdr) && (hdrlen < MIN_HDR_BUF_LEN)) {
        err = -1;
        goto exit;
    }
    if ((body) && (bodylen < MIN_BODY_BUF_LEN)) {
        err = -2;
        goto exit;
    }

    struct curl_Transfer get_data;
    struct curl_Transfer get_header;

	if (!url) {
        err = -3;
        goto exit;
    }

	if (!curl_initialized) {
		res = curl_global_init(CURL_GLOBAL_DEFAULT);
		if (res) {
            err = -4;
            goto exit;
        }
		curl_initialized = 1;
	}

	// Create a curl curl
	curl = curl_easy_init();
	if (curl == NULL) {
        err = -5;
        goto exit;
	}

    init_curl_Transfer(curl, &get_data, body, bodylen, NULL);
    init_curl_Transfer(curl, &get_header, hdr, hdrlen, NULL);

    // initialize custom header list (stating that Expect: 100-continue is not wanted
	headerlist = curl_slist_append(headerlist, hl_buf);

	// set URL that receives this POST
	curl_easy_setopt(curl, CURLOPT_URL, url);

    _set_default_options(curl);

	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &get_header);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &get_data);

	if (formpost) curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

	// Perform the request, res will get the return code
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
    	if (curl_verbose) {
    		ESP_LOGE(CURL_TAG, "curl_easy_perform failed: %s", curl_easy_strerror(res));
    	}
		if (body) snprintf(body, bodylen, "%s", curl_easy_strerror(res));
        err = -7;
        goto exit;
    }

exit:
	// Cleanup
    if (curl) curl_easy_cleanup(curl);
	if (formpost) {
		curl_formfree(formpost);
		formpost = NULL;
	}
	curl_slist_free_all(headerlist);

    return err;
}

//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifdef CONFIG_MICROPY_USE_CURLFTP

//===================================================================================================================
int Curl_FTP(uint8_t upload, char *url, char *user_pass, char *fname, char *hdr, char *body, int hdrlen, int bodylen)
{
#undef DISABLE_SSH_AGENT

	CURL *curl = NULL;
	CURLcode res = 0;
    int err = 0;
	FILE* file = NULL;
	int fsize = 0;

    if ((hdr) && (hdrlen < MIN_HDR_BUF_LEN)) {
        err = -1;
        goto exit;
    }
    if ((body) && (bodylen < MIN_BODY_BUF_LEN)) {
        err = -2;
        goto exit;
    }

    struct curl_Transfer get_data;
    struct curl_Transfer get_header;

	if ((!url) || (!user_pass)) {
        err = -3;
        goto exit;
    }

	if (!curl_initialized) {
		res = curl_global_init(CURL_GLOBAL_DEFAULT);
		if (res) {
            err = -4;
            goto exit;
        }
		curl_initialized = 1;
	}

	// Create a curl curl
	curl = curl_easy_init();
	if (curl == NULL) {
        err = -5;
        goto exit;
	}

    init_curl_Transfer(curl, &get_data, body, bodylen, NULL);
    init_curl_Transfer(curl, &get_header, hdr, hdrlen, NULL);

	if (fname) {
		if (strcmp(fname, "simulate") == 0) {
			get_data.tofile = 1;
			curl_sim_fs = 1;
		}
		else {
			if (upload) {
				// Uploading the file
				struct stat sb;
				if ((stat(fname, &sb) == 0) && (sb.st_size > 0)) {
					fsize = sb.st_size;
					file = fopen(fname, "rb");
				}
			}
			else {
				// Downloading to file (LIST or Get file)
				file = fopen(fname, "wb");
			}
			if (file == NULL) {
	            err = -6;
	            goto exit;
			}
			get_data.file = file;
			get_data.tofile = 1;
			get_data.size = fsize;
			curl_sim_fs = 0;
		}
	}

    curl_easy_setopt(curl, CURLOPT_URL, url);

    _set_default_options(curl);

	/// build a list of commands to pass to libcurl
	//headerlist = curl_slist_append(headerlist, "QUIT");
	curl_easy_setopt(curl, CURLOPT_USERPWD, user_pass);

	//curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
	curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &get_header);
    if (upload) {
    	curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR_RETRY);
        // we want to use our own read function
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, curlRead);
        // specify which file to upload
        curl_easy_setopt(curl, CURLOPT_READDATA, &get_data);
        // enable uploading
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

	    if (fsize > 0) curl_easy_setopt(curl, CURLOPT_INFILESIZE, (long)fsize);
    }
    else {
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &get_data);
    }

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)curl_timeout);

	// Perform the request, res will get the return code
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
    	if (curl_verbose) {
    		ESP_LOGE(CURL_TAG, "curl_easy_perform failed: %s", curl_easy_strerror(res));
    	}
		if (body) snprintf(body, bodylen, "%s", curl_easy_strerror(res));
        err = -7;
        goto exit;
    }

    if (get_data.tofile) {
    	if (curl_progress) {
			double curtime = 0;
			curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &curtime);
			if (upload) {
				ESP_LOGI(CURL_TAG, "* Upload: sent %d B; time=%0.1f s; speed=%0.1f KB/sec", get_data.len, curtime, (float)(((get_data.len*10)/curtime) / 10240.0));
			}
			else {
				ESP_LOGI(CURL_TAG, "* Download: received  %d B; time=%0.1f s; speed=%0.1f KB/sec", get_data.len, curtime, (float)(((get_data.len*10)/curtime) / 10240.0));
			}
    	}

		if (body) {
	        if (upload) {
				if (strcmp(fname, "simulate") == 0) snprintf(body, bodylen, "SIMULATED upload from file; size=%d", get_data.len);
				else snprintf(body, bodylen, "Uploaded file %s, size=%d", fname, fsize);
	        }
	        else {
				if (strcmp(fname, "simulate") == 0) snprintf(body, bodylen, "SIMULATED download to file; size=%d", get_data.len);
				else snprintf(body, bodylen, "Downloaded to file %s, size=%d", fname, get_data.len);
	        }
		}
    }

exit:
	// Cleanup
    if (file) fclose(file);
    if (curl) curl_easy_cleanup(curl);

    return err;
}

#endif

//-------------------
void Curl_cleanup() {
	if (curl_initialized) {
		curl_global_cleanup();
		curl_initialized = 0;
	}
}

#endif // CONFIG_MICROPY_USE_CURL

#ifdef CONFIG_MICROPY_USE_SSH

#include "libssh2.h"
#include "libssh2_sftp.h"

const char *SSH_TAG = "[SSH]";

uint8_t ssh2_verbose = 0;        // show detailed info of what ssh2 functions are doing
uint8_t ssh2_progress = 0;       // show progress during ssh2 transfers
uint16_t ssh2_session_trace = 0;
uint8_t ssh2_session_timeout = 8;
uint32_t ssh2_maxbytes = 300000; // limit download length


// ==== LIBSSH2 functions ====

//------------------------------------------------------------
static int waitsocket(int socket_fd, LIBSSH2_SESSION *session)
{
    struct timeval timeout;
    int rc;
    fd_set fd;
    fd_set *writefd = NULL;
    fd_set *readfd = NULL;
    int dir;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    FD_ZERO(&fd);

    FD_SET(socket_fd, &fd);

    // now make sure we wait in the correct direction
    dir = libssh2_session_block_directions(session);

    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND) readfd = &fd;

    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) writefd = &fd;

    rc = select(socket_fd + 1, readfd, writefd, NULL, &timeout);

    return rc;
}

//--------------------------------------------------------------------------
static void _append_msg(char *messages, char *msg, int msglen, uint8_t type)
{
    if ((messages) && ((strlen(messages) + strlen(msg)+1 < msglen))) {
    	strcat(messages, msg);
    	strcat(messages, "\n");
    }
	if (ssh2_verbose) {
		if (type == ESP_LOG_ERROR) {
			ESP_LOGE(SSH_TAG, "%s", msg);
		}
		else if (type == ESP_LOG_WARN) {
			ESP_LOGW(SSH_TAG, "%s", msg);
		}
		else if (type == ESP_LOG_INFO) {
			ESP_LOGI(SSH_TAG, "%s", msg);
		}
	}
}

//---------------------------------------------------------------------------
static int sock_connect(char *server, char *port, char *messages, int msglen)
{
    int sock=-1;
    int rc;
    char msg[80];
    struct addrinfo *res;
    struct in_addr *addr;
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };

    // Resolve IP address
    rc = getaddrinfo(server, port, &hints, &res);
    if (rc != 0 || res == NULL) {
    	sprintf(msg, "* DNS lookup failed err=%d res=%p", rc, res);
    	_append_msg(messages, msg, msglen, ESP_LOG_ERROR);
        return -1;
    }
    // Print the resolved IP. Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    sprintf(msg, "* DNS lookup succeeded. IP=%s", inet_ntoa(*addr));
	_append_msg(messages, msg, msglen, ESP_LOG_INFO);

    // Create the socket
    sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
    	sprintf(msg, "* Failed to allocate socket.");
    	_append_msg(messages, msg, msglen, ESP_LOG_ERROR);
        freeaddrinfo(res);
        return sock;
    }

    // Connect
    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        sprintf(msg, "* Failed to connect!");
    	_append_msg(messages, msg, msglen, ESP_LOG_ERROR);
        freeaddrinfo(res);
        close(sock);
        return -1;
    }
    sprintf(msg, "* Connected");
	_append_msg(messages, msg, msglen, ESP_LOG_INFO);
    freeaddrinfo(res);

    return sock;
}

//---------------------------------------------------------------------------------------------------------------------------------------------------
static LIBSSH2_SESSION *getSSHSession(int sock, char *username, char *password, char* privkey, char *pubkey, int auth_pw, char *messages, int msglen)
{
    int rc;
    const char *fingerprint;
    LIBSSH2_SESSION *session;
    char msg[96];

    // Create a session instance
    session = libssh2_session_init();
    if(!session) {
        sprintf(msg, "* Failed to create session!");
    	_append_msg(messages, msg, msglen, ESP_LOG_ERROR);
    	return NULL;
    }
    sprintf(msg, "* SSH session created");
	_append_msg(messages, msg, msglen, ESP_LOG_INFO);

	// Set trace level
    libssh2_trace(session, ssh2_session_trace);

    // Set timeout, reset wdt first
	mp_hal_set_wdt_tmo();
	#ifdef CONFIG_MICROPY_USE_TASK_WDT
	esp_task_wdt_reset();
	#endif
	#if CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU0 || CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1
	vTaskDelay(1); // allow other core idle task to reset the watchdog
	#endif
    libssh2_session_set_timeout(session, ssh2_session_timeout*1000);

    // Set preferred key exchange methods
    //libssh2_session_method_pref(session, LIBSSH2_METHOD_KEX, "diffie-hellman-group14-sha1,diffie-hellman-group-exchange-sha256,diffie-hellman-group-exchange-sha1,diffie-hellman-group1-sha1");

    // ... start it up. This will trade welcome banners, exchange keys, and setup crypto, compression, and MAC layers
    rc = libssh2_session_handshake(session, sock);
    if (rc) {
    	libssh2_session_disconnect(session, "Error Shutdown.");
    	libssh2_session_free(session);
        sprintf(msg, "* Failure establishing SSH session: %d", rc);
    	_append_msg(messages, msg, msglen, ESP_LOG_ERROR);
    	return NULL;
    }
    sprintf(msg, "* SSH session established");
	_append_msg(messages, msg, msglen, ESP_LOG_INFO);

    /* At this point we havn't yet authenticated.  The first thing to do
     * is check the hostkey's fingerprint against our known hosts. Your app
     * may have it hard coded, may go to a file, may present it to the
     * user, that's your call
     */
	int fplen = 0, fpofst = 0;
	fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
	if (fingerprint) {
		fplen = 32;
		fpofst = 25;
		sprintf(msg, "* Fingerprint (SHA256): [");
	}
	else {
		fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);
		if (fingerprint) {
			fplen = 20;
			fpofst = 23;
			sprintf(msg, "* Fingerprint (SHA1): [");
		}
		else {
			fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_MD5);
			if (fingerprint) {
				fplen = 16;
				fpofst = 22;
				sprintf(msg, "* Fingerprint (MD5): [");
			}
		}
	}
    if (fplen > 0) {
		for(int i = 0; i < fplen; i++) {
			sprintf(msg+fpofst+(i*2), "%02X", (unsigned char)fingerprint[i]);
		}
		strcat(msg, "]");
		_append_msg(messages, msg, msglen, ESP_LOG_INFO);
    }

    if (auth_pw) {
        // We could authenticate via password
        if (libssh2_userauth_password(session, username, password)) {
        	libssh2_session_disconnect(session, "Error Shutdown.");
        	libssh2_session_free(session);
            sprintf(msg, "* Authentication by password failed.");
        	_append_msg(messages, msg, msglen, ESP_LOG_ERROR);
        	return NULL;
        }
        sprintf(msg,"* Authentication by password succeed.");
    	_append_msg(messages, msg, msglen, ESP_LOG_INFO);
    }
    else {
        // Or by public key
        if (libssh2_userauth_publickey_fromfile(session, username, pubkey, privkey, password)) {
        	libssh2_session_disconnect(session, "Error Shutdown.");
        	libssh2_session_free(session);
            sprintf(msg, "* Authentication by public key failed");
        	_append_msg(messages, msg, msglen, ESP_LOG_ERROR);
        	return NULL;
        }
        sprintf(msg, "* Authentication by public key succeed.");
    	_append_msg(messages, msg, msglen, ESP_LOG_INFO);
    }

    return session;
}

//------------------------------------------------------------------------------------------------------------------------------------------
static int sshDownload(LIBSSH2_CHANNEL *channel, libssh2_struct_stat *fileinfo, FILE *fdd, char *hdr, char *body, int hdrlen, int bodylen) {
    libssh2_struct_stat_size got = 0;
    uint64_t tstart = mp_hal_ticks_ms(), tlatest=0;
    int rc, err = 0;
    uint32_t bodypos = 0;
    char msg[80];
    char mem[1024];

    while(got < fileinfo->st_size) {
        int amount = sizeof(mem);

        if ((fileinfo->st_size -got) < amount) {
            amount = (int)(fileinfo->st_size -got);
        }

        rc = libssh2_channel_read(channel, mem, amount);
        if (rc > 0) {
        	if (fdd != NULL) {
				// === Downloading to file ===
        		if (err == 0) {
        			if ((got + amount) < ssh2_maxbytes) {
						size_t nwrite = fwrite(mem, 1, amount, fdd);
						if (nwrite <= 0) {
							sprintf(msg, "* Download: Error writing to file at %u", (uint32_t)got);
					    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
							err = -10;
						}
        			}
        			else {
						sprintf(msg, "* Max file size exceeded at %u", (uint32_t)got);
				    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
        				err = -11;
        			}
        		}
        	}
        	else {
        		// === Download to buffer ===
        		if (err == 0) {
					if ((bodypos + amount) < bodylen) {
						memcpy(body+bodypos, mem, amount);
						bodypos += amount;
						body[bodypos] = 0;
					}
					else {
						sprintf(msg, "* Buffer full at %u", (uint32_t)got);
				    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
				    	err = -12;
					}
        		}
        	}
        }
        else if( rc < 0) {
			sprintf(msg, "\n* libssh2_channel_read() failed: %d", rc);
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
	        err = -13;
            break;
        }
        got += rc;
    	if (ssh2_progress) {
			if (tlatest == 0) {
				printf("\n");
				tlatest = mp_hal_ticks_ms();
			}
			if ((tlatest + (ssh2_progress*1000)) >= mp_hal_ticks_ms()) {
				tlatest = mp_hal_ticks_ms();
				printf("Download: %u\r", (uint32_t)got);
			}
    	}
    }
    tstart = mp_hal_ticks_ms() - tstart;
	if (ssh2_progress) {
		printf("                          \r");
	}
    sprintf(msg, "* Received: %u bytes in %0.1f sec (%0.3f KB/s)", (uint32_t)got, (float)(tstart / 1000.0), (float)((float)(got)/1024.0/((float)(tstart) / 1000.0)));
	_append_msg(hdr, msg, hdrlen, ESP_LOG_INFO);

    return err;
}

//--------------------------------------------------------------------------------
static int sshUpload(LIBSSH2_CHANNEL *channel, FILE *fdd, char *hdr, int hdrlen) {
    uint64_t tstart = mp_hal_ticks_ms(), tlatest=0;
    int rc, err = 0;
    char msg[80];
    char mem[1024];
    size_t nread;
    char *ptr;
    uint32_t sent = 0;

    do {
        nread = fread(mem, 1, sizeof(mem), fdd);
        if (nread <= 0) {
            // end of file
            break;
        }
        ptr = mem;

        do {
            // write the same data over and over, until error or completion
            rc = libssh2_channel_write(channel, ptr, nread);
            if (rc < 0) {
				sprintf(msg, "* Upload: Error sending: %d at %u", rc, sent);
		    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
		    	err = -10;
                break;
            }
            else {
                // rc indicates how many bytes were written this time
                ptr += rc;
                nread -= rc;
                sent += rc;
            }
        	if (ssh2_progress) {
    			if (tlatest == 0) {
    				printf("\n");
    				tlatest = mp_hal_ticks_ms();
    			}
    			if ((tlatest + (ssh2_progress*1000)) >= mp_hal_ticks_ms()) {
    				tlatest = mp_hal_ticks_ms();
    				printf("Upload: %u\r", sent);
    			}
        	}
        } while (nread);

    } while (1);
    tstart = mp_hal_ticks_ms() - tstart;
	if (ssh2_progress) {
		printf("                          \r");
	}
    sprintf(msg, "* Sent: %u bytes in %0.1f sec (%0.3f KB/s)", sent, (float)(tstart / 1000.0), (float)((float)(sent)/1024.0/((float)(tstart) / 1000.0)));
	_append_msg(hdr, msg, hdrlen, ESP_LOG_INFO);

    libssh2_channel_send_eof(channel);
    libssh2_channel_wait_eof(channel);
    libssh2_channel_wait_closed(channel);

    return err;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------
int ssh_SCP(uint8_t type, char *server, char *port, char * scppath, char *user, char *pass, char *key, char *fname, char *hdr, char *body, int hdrlen, int bodylen)
{

    char msg[80];
    LIBSSH2_SESSION *session;
    libssh2_struct_stat fileinfo;
    FILE *fdd = NULL;
    int fsize = 0;
    hdrlen -=1;
    bodylen -=1;
    int auth = 1;
	char pub_key[128];
    char *privkey = NULL;
    char *pubkey = NULL;
    if (key) {
    	// Check the authentication keys
		struct stat sb_key;
		if ((stat(key, &sb_key) != 0) || (sb_key.st_size = 0)) {
	        sprintf(msg, "* Error opening private key file");
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
            return -5;
		}
    	sprintf(pub_key, key);
    	strcat(pub_key, ".pub");
		if ((stat(pub_key, &sb_key) != 0) || (sb_key.st_size = 0)) {
	        sprintf(msg, "* Error opening public key file");
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
            return -5;
		}
		privkey = key;
		pubkey = pub_key;
		auth = 0;
    }

    if ((fname) && ((type == 0) || (type == 1))){
		if (strcmp(fname, "simulate") != 0) {
			if (type == 1) {
				// Uploading the file
				struct stat sb_local;
				if ((stat(fname, &sb_local) == 0) && (sb_local.st_size > 0)) {
					fsize = sb_local.st_size;
					fdd = fopen(fname, "rb");
				}
			}
			else {
				// Downloading to file (LIST or Get file)
				fdd = fopen(fname, "wb");
			}
			if (fdd == NULL) {
		        sprintf(msg, "* Error opening file");
		    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
	            return -5;
			}
		}
	}

	// ** Initialize libssh2
    int rc = libssh2_init (0);
    if (rc != 0) {
        sprintf(msg, "* libssh2 initialization failed (%d)", rc);
    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
        if (fdd) fclose(fdd);
        return -1;
    }

    int sock = sock_connect(server, port, hdr, hdrlen);
    if (sock < 0) {
        if (fdd) fclose(fdd);
    	libssh2_exit();
    	return -2;
    }
    vTaskDelay(50 / portTICK_RATE_MS);

    // ** Create session
    session = getSSHSession(sock, user, pass, privkey, pubkey, auth, hdr, hdrlen);
    if (session == NULL) {
        if (fdd) fclose(fdd);
    	close(sock);
    	libssh2_exit();
    	return -3;
    }
    vTaskDelay(100 / portTICK_RATE_MS);

    LIBSSH2_CHANNEL *channel = NULL;
    // ** Open session
    if (type == 1) {
    	// === SCP File upload ===
        channel = libssh2_scp_send(session, scppath, 0555, (unsigned long)fsize);
        if (!channel) {
            char *errmsg;
            int errlen;
            int err = libssh2_session_last_error(session, &errmsg, &errlen, 0);
            sprintf(msg, "* Unable to open a session: (%d) %s", err, errmsg);
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
			rc = -4;
            goto shutdown;
        }
		// ** Upload a file via SCP
		rc = sshUpload(channel, fdd, hdr, hdrlen);
		if (fdd != NULL) {
			if (rc == 0) sprintf(body, "Uploaded file %s", fname);
			else sprintf(body, "Error uploading file %s", fname);
		}
    }
    else if (type == 0) {
    	// === SCP File download ===
		channel = libssh2_scp_recv2(session, scppath, &fileinfo);
		if (!channel) {
			sprintf(msg, "* Unable to open a session: %d", libssh2_session_last_errno(session));
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
			rc = -4;
			goto shutdown;
		}

		if (fileinfo.st_size > ssh2_maxbytes) {
			sprintf(msg, "* Warning: file size (%u) > max allowed (%u) !", (uint32_t)fileinfo.st_size, ssh2_maxbytes);
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
		}
		// ** Download a file via SCP
		rc = sshDownload(channel, &fileinfo, fdd, hdr, body, hdrlen, bodylen);
		if (fdd != NULL) {
			if (rc == 0) sprintf(body, "Downloaded file %s", fname);
			else sprintf(body, "Error downloading file %s", fname);
		}
    }
    else if (type == 5) {
    	// === SSH exec, Execute non-blocking on the remove host ===
    	int bytecount = 0;
        char *exitsignal=(char *)"none";

        while (((channel = libssh2_channel_open_session(session)) == NULL) && (libssh2_session_last_error(session,NULL,NULL,0) == LIBSSH2_ERROR_EAGAIN)) {
            waitsocket(sock, session);
        }
        if (channel == NULL) {
			sprintf(msg, "* Channel Error");
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
            rc = -1;
            goto endchannel;
        }
		sprintf(msg, "* Exec: '%s'", scppath);
    	_append_msg(hdr, msg, hdrlen, ESP_LOG_INFO);

		while( (rc = libssh2_channel_exec(channel, scppath)) == LIBSSH2_ERROR_EAGAIN ) {
	        vTaskDelay(2 / portTICK_RATE_MS);
            waitsocket(sock, session);
        }
        if ( rc != 0 ) {
			sprintf(msg, "* Channel Exec Error %d", rc);
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
            goto endchannel;
        }
        char buffer[1024];
        int body_pos = 0;
        for( ;; ) {
            // loop until we block
            int rc;
            do {
                rc = libssh2_channel_read( channel, buffer, sizeof(buffer) );
                if (rc > 0) {
                    if ((body_pos+rc) < bodylen) {
                    	memcpy(body+body_pos, buffer, rc);
                    	body_pos += rc;
                    }
                    bytecount += rc;
                }
            }
            while( rc > 0 );

            // this is due to blocking that would occur otherwise so we loop on this condition
            if ( rc == LIBSSH2_ERROR_EAGAIN ) waitsocket(sock, session);
            else break;
        }
        int exitcode = 127;
        while ( (rc = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN ) waitsocket(sock, session);

        if ( rc == 0 ) {
            exitcode = libssh2_channel_get_exit_status( channel );
            libssh2_channel_get_exit_signal(channel, &exitsignal, NULL, NULL, NULL, NULL, NULL);
        }

        if (exitsignal) {
			sprintf(msg, "* Got signal '%s'", exitsignal);
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_INFO);
			rc = -1;
        }
        else {
			sprintf(msg, "* Exit: %d bytecount: %d", exitcode, bytecount);
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_INFO);
			rc = exitcode;
        }
    }
    else if (type == 4) {
    	// === SFTP mkdir ===
        LIBSSH2_SFTP *sftp_session;

        sftp_session = libssh2_sftp_init(session);
        if (!sftp_session) {
            sprintf(msg, "Unable to init SFTP session");
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
            goto shutdown;
        }
        vTaskDelay(20 / portTICK_RATE_MS);
        // Since we have not set non-blocking, tell libssh2 we are blocking
        libssh2_session_set_blocking(session, 1);
        // Make a directory via SFTP
        rc = libssh2_sftp_mkdir(sftp_session, scppath,
                                LIBSSH2_SFTP_S_IRWXU|
                                LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IXGRP|
                                LIBSSH2_SFTP_S_IROTH|LIBSSH2_SFTP_S_IXOTH);

        if (rc) {
            sprintf(msg, "SFTP mkdir failed\n");
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
        }
        libssh2_sftp_shutdown(sftp_session);
    }
    else if ((type == 2) || (type == 3)) {
    	// === SFTP List ===
        LIBSSH2_SFTP *sftp_session;
        LIBSSH2_SFTP_HANDLE *sftp_handle;

        sftp_session = libssh2_sftp_init(session);
        vTaskDelay(20 / portTICK_RATE_MS);
        if (!sftp_session) {
            sprintf(msg, "Unable to init SFTP session\n");
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
            goto shutdown;
        }
        // Since we have not set non-blocking, tell libssh2 we are blocking
        libssh2_session_set_blocking(session, 1);
        // Request a dir listing via SFTP
        sftp_handle = libssh2_sftp_opendir(sftp_session, scppath);
        if (!sftp_handle) {
            sprintf(msg, "Unable to open dir with SFTP\n");
	    	_append_msg(hdr, msg, hdrlen, ESP_LOG_ERROR);
            goto shutdown;
        }
        int body_pos = 0;
        do {
            char mem[128];
            char longentry[256];
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            attrs.flags = LIBSSH2_SFTP_ATTR_SIZE | LIBSSH2_SFTP_ATTR_ACMODTIME;

            /* loop until we fail */
            rc = libssh2_sftp_readdir_ex(sftp_handle, mem, sizeof(mem), longentry, sizeof(longentry), &attrs);
            if (rc > 0) {
                // rc is the length of the file name in the mem buffer

                if ((longentry[0] != '\0') && (type == 3)) {
                    if ((body_pos+strlen(longentry)) < bodylen) {
                    	sprintf(body+body_pos, "%s\n", longentry);
                    	body_pos += strlen(longentry) + 1;
                    }
                } else {
                    if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
                    	uint32_t prm = attrs.permissions >> 12;
                    	if (prm == 4) sprintf(msg, "D\t");
                    	else if (prm == 8) sprintf(msg, "F\t");
                    	else if (prm == 10) sprintf(msg, "L\t");
                    	else sprintf(msg, "?\t");
                    }
                    else sprintf(msg, "?\t");

                    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) sprintf(msg+strlen(msg), "%llu\t" , (uint64_t)attrs.filesize);
                    else (sprintf(msg, "0\t"));

                    if ((body_pos+strlen(msg)+strlen(mem)+1) < bodylen) {
                    	sprintf(body+body_pos, "%s%s\n", msg, mem);
                    	body_pos += strlen(msg) + strlen(mem) + 1;
                    }
                }
            }
            else
                break;

        } while (1);

        libssh2_sftp_closedir(sftp_handle);
        libssh2_sftp_shutdown(sftp_session);
    }

endchannel:
    if (channel) {
    	libssh2_channel_free(channel);
        channel = NULL;
    }

shutdown:
	if (fdd) fclose(fdd);
	libssh2_session_disconnect(session, "Normal Shutdown.");
	libssh2_session_free(session);
	close(sock);
	libssh2_exit();
	if (ssh2_verbose) {
		ESP_LOGI(SSH_TAG, "All done");
	}

	return rc;
}

#endif // CONFIG_MICROPY_USE_SSH

#endif // defined(CONFIG_MICROPY_USE_CURL) || defined(CONFIG_MICROPY_USE_SSH)
