#include "usart.h"
#include "dma.h"
#include <stm32f407xx.h>

#if !defined(USART_BUFFER_SIZE)
#define USART_BUFFER_SIZE ((uint32_t)512U)
#endif

// TODO Create memorymap section in bootloader RAM for this
volatile uint8_t g_usart2_buffer[USART_BUFFER_SIZE] = {0U};

void usartInit(void)
{
    // APB1 clock is 42MHz
    // Desired baud is 115200
    // From catalog -> 22.8125 value in BRR
    // Mantissa = 22, Fraction = 0.8125*16 = 13
    USART2->BRR = (22U << USART_BRR_DIV_Mantissa_Pos) | 13U;

    // Enable TX, RX, USART
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

// TODO Send data via buffer
// TODO Send data via DMA
// DMA Channels:
// USART2_RX: Ch4 S5
// USART2_TX: Ch4 S6
void usart2SendChar(char c)
{
    while (!(USART2->SR & USART_SR_TXE))
        ;                    // Wait until TXE (Transmit Data Register Empty)
    USART2->DR = (c & 0xFF); // Send data
}

void usart2SendString(const char *str)
{
    while (*str)
    {
        usart2SendChar(*str++);
    }
}

void usart2SendInt(uint32_t num)
{
    char buffer[10];
    int i = 0;

    if (num == 0)
    {
        buffer[i++] = '0';
    }
    else
    {
        // digits to characters in reverse order
        while (num > 0)
        {
            buffer[i++] = (num % 10) + '0';
            num /= 10;
        }
    }

    // send  reverse order
    for (int j = i - 1; j >= 0; j--)
    {
        usart2SendChar(buffer[j]);
    }
}

void usart2SendHex(const uint32_t num)
{
    static const char hex_table[] = "0123456789ABCDEF";
    usart2SendString("0x");

    for (int i = 7U; i >= 0U; i--)
    {
        const uint8_t nibble = (num >> (i * 4)) & 0xF;
        usart2SendChar(hex_table[nibble]);
    }
}
