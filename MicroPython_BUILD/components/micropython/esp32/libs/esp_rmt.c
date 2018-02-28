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

#include <stdio.h>
#include "driver/rmt.h"


static uint8_t rmt_channel_alloc[RMT_CHANNEL_MAX] = {0};



//---------------------------------------------------------------
static bool rmt_channel_check( uint8_t channel, uint8_t num_mem )
{
	if (num_mem == 0 || channel >= RMT_CHANNEL_MAX) {
		// wrong parameter
		return false;
	}
	else if (num_mem == 1) {
		// only one memory block requested
		if (rmt_channel_alloc[channel] == 0) return true;
		else return false;
	}

	return rmt_channel_check( channel-1, num_mem-1);
}

//------------------------------------------
int platform_rmt_allocate( uint8_t num_mem )
{
	int channel;
	uint8_t tag = 1;

	for (channel = RMT_CHANNEL_MAX-1; channel >= 0; channel--) {
		if (rmt_channel_alloc[channel] == 0) {
			if (rmt_channel_check( channel, num_mem )) {
				rmt_channel_alloc[channel] = tag++;
				if (--num_mem == 0) break;
			}
		}
	}

	if (channel >= 0 && num_mem == 0) return channel;
	else return -1;
}

//------------------------------------------
void platform_rmt_release( uint8_t channel )
{
	for ( ; channel < RMT_CHANNEL_MAX; channel++ ) {
		uint8_t tag = rmt_channel_alloc[channel];

		rmt_channel_alloc[channel] = 0;
		if (tag <= 1) break;
	}
}

