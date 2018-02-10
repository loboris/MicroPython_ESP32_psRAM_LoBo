#pragma once

#include "driver/gpio.h"
#include "driver/rmt.h"

#define DIVIDER                4	// 80 MHz clock divider
#define RMT_DURATION_NS     12.5	// minimum time of a single RMT duration based on 80 MHz clock (ns)
#define RMT_PERIOD_NS         50	// minimum bit time based on 80 MHz clock and divider of 4
#define RTM_PIXEL_BUFFER_SIZE  1	//
#define MAX_PULSES			  32	// A channel has a 64 "pulse" buffer - we use half per pass

typedef struct bit_timing {
	uint8_t level0;
	uint16_t duration0;
	uint8_t level1;
	uint16_t duration1;
} bit_timing_t;

typedef struct pixel_timing {
	bit_timing_t mark;
	bit_timing_t space;
	bit_timing_t reset;
} pixel_timing_t;

typedef struct pixel_settings {
	uint8_t *pixels;		// buffer containing pixel values, 3 (RGB) or 4 (RGBW) bytes per pixel
	pixel_timing_t timings;	// timing data from which the pixels BIT data are formed
	uint16_t pixel_count;	// number of used pixels
	uint8_t brightness;		// brightness factor applied to pixel color
	char color_order[5];
	uint8_t nbits;			// number of bits used (24 for RGB devices, 32 for RGBW devices)
} pixel_settings_t;

void np_set_pixel_color(pixel_settings_t *px, uint16_t idx, uint32_t color);
void np_set_pixel_color_hsb(pixel_settings_t *px, uint16_t idx, float hue, float saturation, float brightness);
uint32_t np_get_pixel_color(pixel_settings_t *px, uint16_t idx, uint8_t *white);
void np_show(pixel_settings_t *px, rmt_channel_t channel, uint8_t wait);
void np_clear(pixel_settings_t *px);

int neopixel_init(int gpioNum, rmt_channel_t channel);
void neopixel_deinit(rmt_channel_t channel);

void rgb_to_hsb( uint32_t color, float *hue, float *sat, float *bri );
uint32_t hsb_to_rgb(float hue, float saturation, float brightness);
uint32_t hsb_to_rgb_int(int hue, int sat, int brightness);
