
#include <stdint.h>

// *****************************************************************************
// RMT platform interface

/**
 * @brief Allocate an RMT channel.
 *
 * @param num_mem Number of memory blocks.
 *
 * @return
 *     - Channel number when successful
 *     - -1 if no channel available
 *
 */
int platform_rmt_allocate( uint8_t num_mem );

/**
 * @brief Release a previously allocated RMT channel.
 *
 * @param channel Channel number.
 *
 */
void platform_rmt_release( uint8_t channel );

