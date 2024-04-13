#ifndef __SERIAL_DEBUG_H_
#define __SERIAL_DEBUG_H_

#include "stm32f407xx.h"

#define SERIAL_BUFFER_SIZE 128

typedef struct
{
    char uart_write_buffer[SERIAL_BUFFER_SIZE];
    volatile uint8_t uart_tx_head;
    volatile uint8_t uart_tx_tail;
} serial_debug_data_struct;

void serial_init(void);
void uart_write_string(char *str);
void uart_write_int(uint32_t num);

#endif /* __SERIAL_DEBUG_H_ */