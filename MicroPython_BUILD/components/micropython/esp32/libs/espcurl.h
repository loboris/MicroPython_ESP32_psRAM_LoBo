

#ifndef _ESPCURL_H_
#define _ESPCURL_H_

#include "sdkconfig.h"

#if defined(CONFIG_MICROPY_USE_CURL) || defined(CONFIG_MICROPY_USE_SSH)

#include <stdint.h>
#include "curl/curl.h"

#define MIN_HDR_BODY_BUF_LEN  256
#define GMAIL_SMTP  "smtp.gmail.com"
#define GMAIL_PORT  465


// Some configuration variables
extern uint8_t curl_verbose;   // show detailed info of what curl functions are doing
extern uint8_t curl_progress;  // show progress during transfers
extern uint16_t curl_timeout;  // curl operations timeout in seconds
extern uint32_t curl_maxbytes; // limit download length
extern uint8_t curl_initialized;
extern uint8_t curl_nodecode;

extern int hdr_maxlen;
extern int body_maxlen;

struct curl_httppost *formpost;
struct curl_httppost *lastptr;


// ================
// Public functions
// ================

#ifdef CONFIG_MICROPY_USE_CURL

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

#endif

//==================
void Curl_cleanup();

#ifdef CONFIG_MICROPY_USE_SSH

//=======================================================================================================================================================
int ssh_SCP(uint8_t type, char *server, char *port, char * scppath, char *user, char *pass, char *fname, char *hdr, char *body, int hdrlen, int bodylen);

#endif

//=====================
void checkConnection();

#endif

#endif
