/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */
#ifndef _TRANSPORT_SSL_H_
#define _TRANSPORT_SSL_H_

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief       Create new SSL transport, the transport handle must be release transport_destroy callback
 *
 * @return      the allocated transport_handle_t, or NULL if the handle can not be allocated
 */
transport_handle_t transport_ssl_init();

/**
 * @brief      Set SSL certificate data (as PEM format).
 *             Note that, this function stores the pointer to data, rather than making a copy.
 *             So we need to make sure to keep the data lifetime before cleanup the connection
 *
 * @param      t     ssl transport
 * @param[in]  data  The pem data
 * @param[in]  len   The length
 */
void transport_ssl_set_cert_data(transport_handle_t t, const char *data, int len);


#ifdef __cplusplus
}
#endif
#endif

