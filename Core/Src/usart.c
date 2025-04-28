#include "usart.h"
#include "dma.h"
#include <stdbool.h>
#include <stm32f407xx.h>

#if !defined(USART_BUFFER_SIZE)
#define USART_BUFFER_SIZE ((uint32_t)512U)
#endif

// TODO Create memorymap section in bootloader RAM for this
volatile uint8_t g_usart2_buffer[USART_BUFFER_SIZE] = {0U};

void usartInit(void)
{
    // APB1 clock is 42MHz
    // Desired baud is 921600
    // From catalog -> 2.875 value in BRR
    // Mantissa = 2, Fraction = 0.875*16 = 14
    USART2->BRR = (2U << USART_BRR_DIV_Mantissa_Pos) | 14;

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

    bool printed_non_zero = false;
    for (int idx = 8U; idx > 0U; idx--)
    {
        const uint8_t nibble = (num >> ((idx - 1) * 4)) & 0xF;
        if (nibble != 0 || printed_non_zero)
        {
            usart2SendChar(hex_table[nibble]);
            printed_non_zero = true;
        }
    }

    if (!printed_non_zero)
    {
        usart2SendChar('0');
    }
}
