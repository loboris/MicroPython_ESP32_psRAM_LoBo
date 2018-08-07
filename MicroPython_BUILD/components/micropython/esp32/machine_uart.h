#ifndef INC_MACHINE_UART_H
#define INC_MACHINE_UART_H

#include "driver/uart.h"
#include "py/runtime.h"

#define UART_CB_TYPE_DATA		1
#define UART_CB_TYPE_PATTERN	2
#define UART_CB_TYPE_ERROR		3
#define UART_BUFF_SIZE			256

typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uart_port_t uart_num;
    int8_t bits;
    int8_t parity;
    int8_t stop;
    int8_t tx;
    int8_t rx;
    int8_t rts;
    int8_t cts;
    int data_cb_size;
    uint8_t pattern[16];
    uint8_t pattern_len;
    uint16_t timeout;       // timeout waiting for first char (in ms)
    uint16_t buffer_size;
    uint32_t *data_cb;
    uint32_t *pattern_cb;
    uint32_t *error_cb;
    uint32_t inverted;
    uint8_t end_task;
    uint8_t lineend[3];
} machine_uart_obj_t;

typedef struct _uart_ringbuf_t {
    uint8_t *buf;
    uint16_t size;
    uint16_t iget;
    uint16_t iput;
} uart_ringbuf_t;


char *_uart_read(uart_port_t uart_num, int timeout, char *lnend, char *lnstart);
int match_pattern(uint8_t *text, int text_length, uint8_t *pattern, int pattern_length);
int uart_buf_get(uart_ringbuf_t *r, uint8_t *dest, uint16_t len);
int uart_buf_put(uart_ringbuf_t *r, uint8_t *source, uint16_t len);

#endif
