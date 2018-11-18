/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
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

/* ****************************************************************************
 *
 * ESP32 platform interface for DHT temperature & humidity sensors
 *
 * Copyright (c) 2017, Arnim Laeuger
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * ****************************************************************************/


#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt.h"
#include "driver/gpio.h"
#include "libs/esp_rmt.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "mphalport.h"


#undef DHT_DEBUG

#define PLATFORM_ERR	0
#define PLATFORM_OK		1

#define PLATFORM_DHT11_WAKEUP_MS 22
#define PLATFORM_DHT2X_WAKEUP_MS 2


// RX idle threshold [us]
// needs to be larger than any duration occurring during bit slots
// datasheet specs up to 200us for "Bus master has released time"
#define DHT_DURATION_RX_IDLE (250)

// zero bit duration threshold [us]
// a high phase
// * shorter than this is detected as a zero bit
// * longer than this is detected as a one bit
#define DHT_DURATION_ZERO (50)


// grouped information for RMT management
static struct {
  int channel;
  RingbufHandle_t rb;
  int gpio;
} dht_rmt = {-1, NULL, -1};



typedef enum {
  LDHT_OK = 0,
  LDHT_ERROR_CHECKSUM = -1,
  LDHT_ERROR_TIMEOUT = -2,
  LDHT_INVALID_VALUE = -999
} ldht_result_t;

typedef enum {
  LDHT11,
  LDHT2X
} ldht_type_t;


//----------------------------
static void dht_deinit( void )
{
  // drive idle level 1
  gpio_set_level( dht_rmt.gpio, 1 );

  rmt_rx_stop( dht_rmt.channel );
  rmt_driver_uninstall( dht_rmt.channel );

  platform_rmt_release( dht_rmt.channel );

  // invalidate channel and gpio assignments
  dht_rmt.channel = -1;
  dht_rmt.gpio = -1;
}

//-------------------------------------
static int dht_init( uint8_t gpio_num )
{
  // acquire an RMT module for RX
  if ((dht_rmt.channel = platform_rmt_allocate( 1 )) >= 0) {

#ifdef DHT_DEBUG
    mp_printf(&mp_plat_print, "[dht] RMT RX channel: %d\n", dht_rmt.channel);
#endif

    rmt_config_t rmt_rx;
    rmt_rx.channel = dht_rmt.channel;
    rmt_rx.gpio_num = gpio_num;
    rmt_rx.clk_div = 80;  // base period is 1us
    rmt_rx.mem_block_num = 1;
    rmt_rx.rmt_mode = RMT_MODE_RX;
    rmt_rx.rx_config.filter_en = true;
    rmt_rx.rx_config.filter_ticks_thresh = 30;
    rmt_rx.rx_config.idle_threshold = DHT_DURATION_RX_IDLE;
    if (rmt_config( &rmt_rx ) == ESP_OK) {
      if (rmt_driver_install( rmt_rx.channel, 512, ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_SHARED ) == ESP_OK) {

        //rmt_get_ringbuf_handler( dht_rmt.channel, &dht_rmt.rb );
        rmt_get_ringbuf_handle( dht_rmt.channel, &dht_rmt.rb );

        dht_rmt.gpio = gpio_num;

        // use gpio for TX direction
        // drive idle level 1
        gpio_set_level( gpio_num, 1 );
        gpio_pullup_dis( gpio_num );
        gpio_pulldown_dis( gpio_num );
        gpio_set_direction( gpio_num, GPIO_MODE_INPUT_OUTPUT_OD );
        gpio_set_intr_type( gpio_num, GPIO_INTR_DISABLE );

        return PLATFORM_OK;

      }
    }

    platform_rmt_release( dht_rmt.channel );
  }

  return PLATFORM_ERR;
}

//-------------------------------------------------------------------------
int platform_dht_read( uint8_t gpio_num, uint8_t wakeup_ms, uint8_t *data )
{
  if (dht_init( gpio_num ) != PLATFORM_OK)
    return PLATFORM_ERR;

  // send start signal and arm RX channel
  TickType_t xDelay = wakeup_ms / portTICK_PERIOD_MS;
  if (xDelay == 0) xDelay = 1;
  gpio_set_level( gpio_num, 0 );         // pull wire low
  vTaskDelay( xDelay );                  // time low phase
  rmt_rx_start( dht_rmt.channel, true ); // arm RX channel
  gpio_set_level( gpio_num, 1 );         // release wire

  // wait for incoming bit stream
  size_t rx_size;
  rmt_item32_t* rx_items = (rmt_item32_t *)xRingbufferReceive( dht_rmt.rb, &rx_size, pdMS_TO_TICKS( 100 ) );

  // default is "no error"
  // error conditions have to overwrite this with PLATFORM_ERR
  int res = PLATFORM_OK;

  if (rx_items) {

	#ifdef DHT_DEBUG
    mp_printf(&mp_plat_print, "[dht] rx_items received: %d\n", rx_size);
    for (size_t i = 0; i < rx_size / 4; i++) {
      mp_printf(&mp_plat_print, "[dht] level: %d, duration %d", rx_items[i].level0, rx_items[i].duration0);
      mp_printf(&mp_plat_print, " level: %d, duration %d\n", rx_items[i].level1, rx_items[i].duration1);
    }
	#endif

    // we expect 40 bits of payload plus a response bit and two edges for start and stop signals
    // each bit on the wire consumes 2 rmt samples (and 2 rmt samples stretch over 4 bytes)
    if (rx_size >= (5*8 + 1+1) * 4) {

      // check framing
      if (rx_items[ 0].level0 == 1 &&                            // rising edge of the start signal
          rx_items[ 0].level1 == 0 && rx_items[1].level0 == 1 && // response signal
          rx_items[41].level1 == 0) {                            // falling edge of stop signal

		#ifdef DHT_DEBUG
        mp_printf(&mp_plat_print, "[dht] data: ");
		#endif
        // run through the bytes
        for (size_t byte = 0; byte < 5 && res == PLATFORM_OK; byte++) {
          size_t bit_pos = 1 + byte*8;
          data[byte] = 0;

          // decode the bits inside a byte
          for (size_t bit = 0; bit < 8; bit++, bit_pos++) {
            if (rx_items[bit_pos].level1 != 0) {
              // not a falling edge, terminate decoding
              res = PLATFORM_ERR;
              break;
            }
            // ignore duration of low level

            // data is sent MSB first
            data[byte] <<= 1;
            if (rx_items[bit_pos + 1].level0 == 1 && rx_items[bit_pos + 1].duration0 > DHT_DURATION_ZERO)
              data[byte] |= 1;
          }

		  #ifdef DHT_DEBUG
          mp_printf(&mp_plat_print, "%02x ", data[byte]);
		  #endif
        }
		#ifdef DHT_DEBUG
        mp_printf(&mp_plat_print, "\n");
		#endif

        // all done

      } else {
        // framing mismatch on start, response, or stop signals
        res = PLATFORM_ERR;
      }

    } else {
      // too few bits received
      res = PLATFORM_ERR;
    }

    vRingbufferReturnItem( dht_rmt.rb, (void *)rx_items );
  } else {
    // time out occurred, this indicates an unconnected / misconfigured bus
    res = PLATFORM_ERR;
  }

  dht_deinit();

  return res;
}


//-------------------------------------------------------------------------
static int ldht_compute_data11( uint8_t *data, double *temp, double *humi )
{
  *humi = data[0];
  *temp = data[2];

  uint8_t sum = data[0] + data[1] + data[2] + data[3];
  return sum == data[4] ? LDHT_OK : LDHT_ERROR_CHECKSUM;
}

//-------------------------------------------------------------------------
static int ldht_compute_data2x( uint8_t *data, double *temp, double *humi )
{
  *humi = (double)((data[0] * 256 + data[1])) / 10;
  *temp = (double)(((data[2] & 0x7f) * 256 + data[3])) / 10;

  if (data[2] & 0x80)
    *temp = - *temp;

  uint8_t sum = data[0] + data[1] + data[2] + data[3];
  return sum == data[4] ? LDHT_OK : LDHT_ERROR_CHECKSUM;
}


// ==== MicroPython bindings for DHT ===========================================

typedef struct _machine_dht_obj_t {
    mp_obj_base_t base;
    uint8_t pin;
    uint8_t type;
} machine_dht_obj_t;


//--------------------------------------------------------------------------------------------
STATIC void machine_dht_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    machine_dht_obj_t *self = self_in;
    char stype[8];
    if (self->type == LDHT11) sprintf(stype, "DHT11");
    else if (self->type == LDHT2X) sprintf(stype, "DHT2X");
    else sprintf(stype, "??");
    mp_printf(print, "DHT(Pin=%u, Type=%s)", self->pin, stype);
}


//-------------------------------------------------------
STATIC const mp_arg_t machine_dht_init_allowed_args[] = {
		{ MP_QSTR_pin,   MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
		{ MP_QSTR_type,                    MP_ARG_INT, {.u_int = LDHT11} },
};

//------------------------------------------------------------------------------------------------------------
mp_obj_t machine_dht_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
	enum { ARG_pin, ARG_type };

	mp_arg_val_t args[MP_ARRAY_SIZE(machine_dht_init_allowed_args)];
	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(machine_dht_init_allowed_args), machine_dht_init_allowed_args, args);

    uint8_t pin;
    uint8_t dht_type = args[ARG_type].u_int;

    pin = machine_pin_get_gpio(args[ARG_pin].u_obj);

    // Setup the DHT object
    machine_dht_obj_t *self = m_new_obj(machine_dht_obj_t );
    self->base.type = &machine_dht_type;
    self->pin = pin;
    self->type = dht_type;

    return MP_OBJ_FROM_PTR(self);
}

//------------------------------------------------
STATIC mp_obj_t machine_dht_read(mp_obj_t self_in)
{
    machine_dht_obj_t *self = self_in;

    uint8_t data[5];
    double temp = -999.0, humi= -999.0;
   	mp_obj_t tuple[3];

    int res = platform_dht_read( self->pin, self->type == LDHT11 ? PLATFORM_DHT11_WAKEUP_MS : PLATFORM_DHT2X_WAKEUP_MS, data );

    if (res == PLATFORM_OK) {
		switch (self->type) {
		case LDHT11:
		  res = ldht_compute_data11( data, &temp, &humi );
		  break;
		case LDHT2X:
		  res = ldht_compute_data2x( data, &temp, &humi );
		  break;
		default:
		  res = LDHT_INVALID_VALUE;
		  temp = 0; humi = 0;
		  break;
		}
		if (res == LDHT_OK) tuple[0] = mp_const_true;
		else tuple[0] = mp_const_false;
    }
    else {
       	tuple[0] = mp_const_false;
    }

   	tuple[1] = mp_obj_new_float(temp);
   	tuple[2] = mp_obj_new_float(humi);

   	return mp_obj_new_tuple(3, tuple);
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_dht_read_obj, machine_dht_read);


//---------------------------------------------------------------------
STATIC mp_obj_t machine_dht_readinto(mp_obj_t self_in, mp_obj_t buf_in)
{
    machine_dht_obj_t *self = self_in;

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    if (bufinfo.len < 5) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "buffer too small"));
    }

    int res = platform_dht_read( self->pin, self->type == LDHT11 ? PLATFORM_DHT11_WAKEUP_MS : PLATFORM_DHT2X_WAKEUP_MS, bufinfo.buf );

    if (res != PLATFORM_OK) {
        return mp_const_false;
    }

    return mp_const_true;
}
MP_DEFINE_CONST_FUN_OBJ_2(machine_dht_readinto_obj, machine_dht_readinto);

//
//================================================================
STATIC const mp_rom_map_elem_t machine_dht_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readinto),	(mp_obj_t)&machine_dht_readinto_obj },
    { MP_ROM_QSTR(MP_QSTR_read),		(mp_obj_t)&machine_dht_read_obj },

	{ MP_ROM_QSTR(MP_QSTR_DHT11), MP_ROM_INT(LDHT11) },
	{ MP_ROM_QSTR(MP_QSTR_DHT2X), MP_ROM_INT(LDHT2X) },
};

STATIC MP_DEFINE_CONST_DICT(machine_dht_locals_dict, machine_dht_locals_dict_table);

//======================================
const mp_obj_type_t machine_dht_type = {
    { &mp_type_type },
    .name = MP_QSTR_DHT,
    .print = machine_dht_print,
    .make_new = machine_dht_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_dht_locals_dict,
};
