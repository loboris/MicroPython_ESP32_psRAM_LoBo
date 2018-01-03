/*
 * Copyright (c) 2016, Pycom Limited.
 *
 * This software is licensed under the GNU GPL version 3 or any
 * later version, with permitted additional terms. For more information
 * see the Pycom Licence v1.0 document supplied with this file, or
 * available at https://www.pycom.io/opensource/licensing
 */

#include <stdint.h>

#include "py/obj.h"
#include "random.h"
#include "esp_system.h"

/******************************************************************************
* LOCAL TYPES
******************************************************************************/
typedef union _rng_id_t {
    uint32_t       id32;
    uint16_t       id16[3];
    uint8_t        id8[6];
} rng_id_t;

/******************************************************************************
* LOCAL VARIABLES
******************************************************************************/
static uint32_t s_seed;

/******************************************************************************
* LOCAL FUNCTION DECLARATIONS
******************************************************************************/
STATIC uint32_t lfsr (uint32_t input);

/******************************************************************************
* PRIVATE FUNCTIONS
******************************************************************************/
STATIC uint32_t lfsr (uint32_t input) {
    return (input >> 1) ^ (-(input & 0x01) & 0x00E10000);
}

/******************************************************************************/
// Micro Python bindings;

STATIC mp_obj_t machine_rng_get(void) {
    return mp_obj_new_int(rng_get());
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_rng_get_obj, machine_rng_get);

/******************************************************************************
* PUBLIC FUNCTIONS
******************************************************************************/
void rng_init0 (void) {
// FIXME
    rng_id_t juggler;
//    uint32_t seconds;
//    uint16_t mseconds;

    // get the seconds and the milliseconds from the RTC
//    pyb_rtc_get_time(&seconds, &mseconds);

    esp_efuse_mac_get_default(juggler.id8);

    // flatten the 48-bit board identification to 24 bits
    juggler.id16[0] ^= juggler.id16[2];

    juggler.id8[0]  ^= juggler.id8[3];
    juggler.id8[1]  ^= juggler.id8[4];
    juggler.id8[2]  ^= juggler.id8[5];

    s_seed = juggler.id32 & 0x00FFFFFF;
//    s_seed += (seconds & 0x000FFFFF) + mseconds;

    // the seed must never be zero
    if (s_seed == 0) {
        s_seed = 1;
    }
}

uint32_t rng_get (void) {
    s_seed = lfsr( s_seed );
    return s_seed;
}
