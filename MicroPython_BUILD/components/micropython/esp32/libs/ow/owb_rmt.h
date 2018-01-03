#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "driver/rmt.h"

typedef struct {
  int tx_channel;
  int rx_channel;
  RingbufHandle_t rb;
  int gpio;

  OneWireBus bus;
} owb_rmt_driver_info;

OneWireBus* owb_rmt_initialize( owb_rmt_driver_info *info, uint8_t gpio_num,
                                rmt_channel_t tx_channel, rmt_channel_t rx_channel);
