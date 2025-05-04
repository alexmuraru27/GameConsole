#ifndef __USART_COMM_H
#define __USART_COMM_H
#include <stdint.h>

void usartInit(void);
void usart2BufferFlush(void);
void debugChar(char c);
void debugString(const char *str);
void debugInt(uint32_t num);
void debugHex(uint32_t num);
void debugBinary(uint32_t num, uint8_t width);
#endif /* __USART_COMM_H */