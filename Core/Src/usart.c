#include "usart.h"
#include <stdbool.h>
#include <stm32f407xx.h>
#include <string.h>

#if !defined(USART_BUFFER_SIZE)
#define USART_BUFFER_SIZE ((uint32_t)512U)
#endif

// TODO Create memorymap section in bootloader RAM for this
// TODO refactor this to use either circular buffer, either DBM
// TODO interrupt handler is huge!
uint8_t g_usart2_buffer_a[USART_BUFFER_SIZE] = {0U};
uint8_t g_usart2_buffer_b[USART_BUFFER_SIZE] = {0U};
uint8_t *g_usart2_active_buffer = g_usart2_buffer_a; // CPU writes here
volatile uint16_t g_usart2_active_buffer_pos = 0;
volatile uint8_t g_usart2_dma_busy = 0;

void usart2BufferFlush(void)
{
    if (g_usart2_dma_busy)
        return; // DMA busy -> don't flush yet!

    NVIC_DisableIRQ(DMA1_Stream6_IRQn);
    DMA1_Stream6->CR &= ~DMA_SxCR_EN;
    DMA1_Stream6->M0AR = (uint32_t)g_usart2_active_buffer;
    DMA1_Stream6->NDTR = g_usart2_active_buffer_pos;

    // Swap active buffer
    g_usart2_active_buffer = (g_usart2_active_buffer == g_usart2_buffer_a) ? g_usart2_buffer_b : g_usart2_buffer_a;
    g_usart2_active_buffer_pos = 0;

    // Start DMA transfer
    g_usart2_dma_busy = 1;
    NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    DMA1_Stream6->CR |= DMA_SxCR_EN;
}

static void usart2Send(const uint8_t *data, uint16_t size)
{
    if (size > (USART_BUFFER_SIZE - g_usart2_active_buffer_pos))
    {
        // Buffer full! Maybe flush first or discard
        return;
    }

    memcpy(&g_usart2_active_buffer[g_usart2_active_buffer_pos], data, size);
    g_usart2_active_buffer_pos += size;

    // Optional: if DMA not busy, flush immediately
    usart2BufferFlush();
}

void usartInit(void)
{
    // APB1 clock is 42MHz
    // Desired baud is 921600
    // From catalog -> 2.875 value in BRR
    // Mantissa = 2, Fraction = 0.875*16 = 14
    USART2->BRR = (2U << USART_BRR_DIV_Mantissa_Pos) | 14;

    // Enable TX, RX, USART
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    // Enable DMA transmitter
    USART2->CR3 |= USART_CR3_DMAT;
}

void debugChar(char c)
{
    usart2Send((uint8_t *)&c, 1U);
}

void debugString(const char *str)
{
    usart2Send((uint8_t *)str, strlen(str));
}

void debugInt(uint32_t num)
{
    char buffer[12];
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
    for (uint8_t j = 0U; j < i / 2U; j++)
    {
        char temp = buffer[j];
        buffer[j] = buffer[i - 1U - j];
        buffer[i - 1 - j] = temp;
    }

    buffer[i] = '\0';
    debugString(buffer);
}

void debugHex(const uint32_t num)
{
    static const char hex_table[] = "0123456789ABCDEF";
    char buffer[12];
    uint8_t bufIndex = 0;

    buffer[bufIndex++] = '0';
    buffer[bufIndex++] = 'x';

    bool printed_non_zero = false;
    for (int idx = 8U; idx > 0U; idx--)
    {
        const uint8_t nibble = (num >> ((idx - 1) * 4)) & 0xF;
        if (nibble != 0 || printed_non_zero)
        {
            buffer[bufIndex++] = hex_table[nibble];
            printed_non_zero = true;
        }
    }

    if (!printed_non_zero)
    {
        buffer[bufIndex++] = '0';
    }
    buffer[bufIndex] = '\0';
    debugString(buffer);
}

void debugBinary(uint32_t num, uint8_t width)
{
    if (width != 8 && width != 16 && width != 32)
        return;

    char buffer[40];
    uint8_t bufIndex = 0;

    for (uint8_t i = width; i > 0; i--)
    {
        buffer[bufIndex++] = (num & (1U << (i - 1))) ? '1' : '0';

        if ((i - 1) % 4 == 0 && (i - 1) != 0)
        {
            buffer[bufIndex++] = ' ';
        }
    }

    buffer[bufIndex] = '\0'; // Null-terminate the string
    debugString(buffer);
}