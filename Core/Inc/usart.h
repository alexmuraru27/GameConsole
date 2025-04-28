#ifndef __USART_COMM_H
#define __USART_COMM_H
#include <stdint.h>

void usartInit(void);
void usart2SendChar(char c);
void usart2SendString(const char *str);
void usart2SendInt(uint32_t num);
#endif /* __USART_COMM_H */