/*
 * Copyright (c) 2016, Pycom Limited.
 *
 * This software is licensed under the GNU GPL version 3 or any
 * later version, with permitted additional terms. For more information
 * see the Pycom Licence v1.0 document supplied with this file, or
 * available at https://www.pycom.io/opensource/licensing
 */

#ifndef __RANDOM_H
#define __RANDOM_H

void rng_init0 (void);
uint32_t rng_get (void);

MP_DECLARE_CONST_FUN_OBJ_0(machine_rng_get_obj);

#endif // __RANDOM_H
