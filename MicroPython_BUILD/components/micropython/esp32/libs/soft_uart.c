*
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

// UART.C
//
// Generic software uart written in C, requiring a timer set to 3 times
// the baud rate, and two software read/write pins for the receive and
// transmit functions.
//
// * Received characters are buffered
// * putchar(), getchar(), kbhit() and flush_input_buffer() are available
// * There is a facility for background processing while waiting for input
//
// Colin Gittins, Software Engineer, Halliburton Energy Services
//
// The baud rate can be configured by changing the BAUD_RATE macro as
// follows:
//
// #define BAUD_RATE            19200.0
//
// The function init_uart() must be called before any comms can take place
//
// Interface routines required:
// 1. get_rx_pin_status()
//    Returns 0 or 1 dependent on whether the receive pin is high or low.
// 2. set_tx_pin_high()
//    Sets the transmit pin to the high state.
// 3. set_tx_pin_low()
//    Sets the transmit pin to the low state.
// 4. idle()
//    Background functions to execute while waiting for input.
// 5. timer_set( BAUD_RATE )
//    Sets the timer to 3 times the baud rate.
// 6. set_timer_interrupt( timer_isr )
//    Enables the timer interrupt.
//
// Functions provided:
// 1. void flush_input_buffer( void )
//    Clears the contents of the input buffer.
// 2. char kbhit( void )
//    Tests whether an input character has been received.
// 3. char getchar( void )
//    Reads a character from the input buffer, waiting if necessary.
// 4. void turn_rx_on( void )
//    Turns on the receive function.
// 5. void turn_rx_off( void )
//    Turns off the receive function.
// 6. void putchar( char )
//    Writes a character to the serial port.

#include "sdkconfig.h"

#ifdef CONFIG_USE_SOFTUART

#include <stdio.h>
#include "driver/timer.h"
#include "driver/gpio.h"

#define BAUD_RATE       19200.0

#define IN_BUF_SIZE     256

#define TRUE 1
#define FALSE 0

static unsigned char	inbuf[IN_BUF_SIZE];
static unsigned char    qin = 0;
static unsigned char    qout = 0;

static char             flag_rx_waiting_for_stop_bit;
static char             flag_rx_off;
static char             rx_mask;
static char             flag_rx_ready;
static char             flag_tx_ready;
static char             timer_rx_ctr;
static char             timer_tx_ctr;
static char             bits_left_in_rx;
static char             bits_left_in_tx;
static char             rx_num_of_bits;
static char             tx_num_of_bits;
static char             internal_rx_buffer;
static char             internal_tx_buffer;
static char             user_tx_buffer;

static int bdrate = 19200;
static intr_handle_t int_handle;
static uint8_t rx_gpio_num = 26;
static uint8_t tx_gpio_num = 27;
static int bdr_fact = 3;
static uint8_t tmr_group = 1;
static uint8_t tmr_id = 1;

// Interface routines required:
// 1. get_rx_pin_status()
//    Returns 0 or 1 dependent on whether the receive pin is high or low.
//#define get_rx_pin_status()
// 2. set_tx_pin_high()
//    Sets the transmit pin to the high state.
//#define set_tx_pin_high()
// 3. set_tx_pin_low()
//    Sets the transmit pin to the low state.
//#define set_tx_pin_high()
// 4. idle()
#define idle
//    Background functions to execute while waiting for input.
// 5. timer_set( BAUD_RATE )
//    Sets the timer to 3 times the baud rate.
//#define timer_set( BAUD_RATE )
// 6. set_timer_interrupt( timer_isr )
//    Enables the timer interrupt.
//#define set_timer_interrupt( timer_isr )

#define TIMER_FLAGS			0

//---------------------------------------------------------
static void timer_set()
{
    timer_config_t config;
    config.counter_dir = TIMER_COUNT_UP;
    config.intr_type = TIMER_INTR_LEVEL;
    config.counter_en = TIMER_PAUSE;
	config.alarm_en = TIMER_ALARM_EN;
	config.auto_reload = 1;
	config.divider = 2;

	uint64_t alarm = 40000000 / (bdrate * bdr_fact);
	uint64_t modalarm  = 40000000 % (bdrate * bdr_fact);
	if (modalarm > (bdrate / 2)) alarm +=1;

    timer_init(1, 1, &config);

    // Timer's counter will initially start from value below.
    // Also, if auto_reload is set, this value will be automatically reload on alarm
    timer_set_counter_value(tmr_group, tmr_id, 0x00000000ULL);

	// Configure the alarm value and the interrupt on alarm.
	timer_set_alarm_value(tmr_group, tmr_id, alarm);
	// Enable timer interrupt
	timer_enable_intr(tmr_group, tmr_id);
	// Register interrupt callback
	timer_isr_register(tmr_group, tmr_id, timer_isr, NULL, TIMER_FLAGS, &int_handle);
	timer_start(tmr_group, tmr_id);
}

//-----------------------------------
static void IRAM_ATTR timer_isr(void)
{
    uint8_t mask, start_bit, flag_in;

    // Transmitter Section
    if ( flag_tx_ready ) {
        if ( --timer_tx_ctr<=0 ) {
            mask = internal_tx_buffer & 1;
            internal_tx_buffer >>= 1;
            gpio_set_level(tx_gpio_num, mask);
            timer_tx_ctr = bdr_fact;
            if ( --bits_left_in_tx<=0 ) flag_tx_ready = FALSE;
        }
    }

    // Receiver Section
    if ( flag_rx_off==FALSE ) {
        if ( flag_rx_waiting_for_stop_bit ) {
            if ( --timer_rx_ctr<=0 ) {
                flag_rx_waiting_for_stop_bit = FALSE;
                flag_rx_ready = FALSE;
                internal_rx_buffer &= 0xFF;
                if ( internal_rx_buffer!=0xC2 ) {
                    inbuf[qin] = internal_rx_buffer;
                    if ( ++qin>=IN_BUF_SIZE ) qin = 0;
                }
            }
        }
        else { // rx_test_busy
            if ( flag_rx_ready==FALSE )
                {
                start_bit = gpio_get_level(rx_gpio_num);
                // Test for Start Bit
                if ( start_bit==0 ) {
                    flag_rx_ready = TRUE;
                    internal_rx_buffer = 0;
                    timer_rx_ctr = 4;
                    bits_left_in_rx = rx_num_of_bits;
                    rx_mask = 1;
                }
            }
            else { // rx_busy
                if ( --timer_rx_ctr<=0 ) { // rcv
                    timer_rx_ctr = 3;
                    flag_in = gpio_get_level(rx_gpio_num);
                    if ( flag_in ) internal_rx_buffer |= rx_mask;
                    rx_mask <<= 1;
                    if ( --bits_left_in_rx<=0 ) flag_rx_waiting_for_stop_bit = TRUE;
                }
            }
        }
    }
}

//-------------------------
void soft_uart_init(uint16_t timer, uint8_t rx_num, uint8_t tx_num, int bdr)
{
    flag_tx_ready = FALSE;
    flag_rx_ready = FALSE;
    flag_rx_waiting_for_stop_bit = FALSE;
    flag_rx_off = FALSE;
    rx_num_of_bits = 10;
    tx_num_of_bits = 10;
    rx_gpio_num = rx_num;
    tx_gpio_num = tx_num;
    bdrate = bdr;

    gpio_pad_select_gpio(rx_gpio_num);
    gpio_pad_select_gpio(tx_gpio_num);
    gpio_set_level(rx_gpio_num, 1);
    gpio_set_level(tx_gpio_num, 1);
    gpio_set_direction(rx_gpio_num, GPIO_MODE_INPUT);
    gpio_set_pull_mode(rx_gpio_num, GPIO_PULLUP_ONLY);
    gpio_set_direction(tx_gpio_num, GPIO_MODE_OUTPUT);

    timer_set(bdrate);
}


//-------------------
char _getchar( void )
{
    char        ch;

    do
        {
        while ( qout==qin )
            {
            idle();
            }
        ch = inbuf[qout] & 0xFF;
        if ( ++qout>=IN_BUF_SIZE )
            {
            qout = 0;
            }
        }
    while ( ch==0x0A || ch==0xC2 );
    return( ch );
}

void _putchar( char ch )
{
    while ( flag_tx_ready );
    user_tx_buffer = ch;

    // invoke_UART_transmit
    timer_tx_ctr = 3;
    bits_left_in_tx = tx_num_of_bits;
    internal_tx_buffer = (user_tx_buffer<<1) | 0x200;
    flag_tx_ready = TRUE;
}

void flush_input_buffer( void )
{
    qin = 0;
    qout = 0;
}

char kbhit( void )
{
    return( qin!=qout );
}

void turn_rx_on( void )
{
    flag_rx_off = FALSE;
}

void turn_rx_off( void )
{
	flag_rx_off = TRUE;
}

#endif
