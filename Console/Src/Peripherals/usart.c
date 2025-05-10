#include "usart.h"
#include <stdbool.h>
#include <stm32f407xx.h>
#include <string.h>

#if !defined(USART_BUFFER_SIZE)
#define USART_BUFFER_SIZE ((uint32_t)512U)
#endif

uint8_t s_usart2_buffer_a[USART_BUFFER_SIZE] = {0U};
uint8_t s_usart2_buffer_b[USART_BUFFER_SIZE] = {0U};
uint8_t *s_usart2_active_buffer = s_usart2_buffer_a; // CPU writes here
volatile uint16_t s_usart2_active_buffer_pos = 0;
volatile uint8_t s_usart2_dma_busy = 0;

void usart2SetDmaFree(void)
{
    s_usart2_dma_busy = 0U;
}
static void usart2BufferFlush(void)
{
    // if DMA is busy, don't flush
    if (s_usart2_dma_busy)
        return;

    // if the buffer is empty, there is nothing to write
    if (s_usart2_active_buffer_pos == 0)
    {
        return;
    }

    DMA1_Stream6->CR &= ~DMA_SxCR_EN;
    DMA1_Stream6->M0AR = (uint32_t)s_usart2_active_buffer;
    DMA1_Stream6->NDTR = s_usart2_active_buffer_pos;

    // Swap active buffer
    s_usart2_active_buffer = (s_usart2_active_buffer == s_usart2_buffer_a) ? s_usart2_buffer_b : s_usart2_buffer_a;
    s_usart2_active_buffer_pos = 0;

    // Start DMA transfer
    s_usart2_dma_busy = 1;
    DMA1_Stream6->CR |= DMA_SxCR_EN;
}

static void usart2Send(const uint8_t *data, uint16_t size)
{
    // If enough place inside the buffer, fill it
    if (size < (USART_BUFFER_SIZE - s_usart2_active_buffer_pos))
    {
        memcpy(&s_usart2_active_buffer[s_usart2_active_buffer_pos], data, size);
        s_usart2_active_buffer_pos += size;
    }

    // Try to always trigger a flush
    usart2BufferFlush();
}

static void usart2Init(void)
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

static void debugBuffer(const uint8_t *data, const uint16_t size)
{
    usart2Send((uint8_t *)data, size);
}

void debugChar(char c)
{
    debugBuffer((uint8_t *)&c, 1U);
}

void debugString(const char *str)
{
    debugBuffer((uint8_t *)str, strlen(str));
}

void debugInt(uint32_t num)
{
    uint8_t buffer[12];
    uint8_t i = 0;

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
        uint8_t temp = buffer[j];
        buffer[j] = buffer[i - 1U - j];
        buffer[i - 1 - j] = temp;
    }

    debugBuffer(buffer, i);
}

void debugHex(const uint32_t num)
{
    static const uint8_t hex_table[] = "0123456789ABCDEF";
    uint8_t buffer[12];
    uint8_t buf_index = 0;

    buffer[buf_index++] = '0';
    buffer[buf_index++] = 'x';

    bool printed_non_zero = false;
    for (uint8_t idx = 8U; idx > 0U; idx--)
    {
        const uint8_t nibble = (num >> ((idx - 1) * 4)) & 0xF;
        if (nibble != 0 || printed_non_zero)
        {
            buffer[buf_index++] = hex_table[nibble];
            printed_non_zero = true;
        }
    }

    if (!printed_non_zero)
    {
        buffer[buf_index++] = '0';
    }

    debugBuffer(buffer, buf_index);
}

void debugBinary(uint32_t num, uint8_t width)
{
    if (width != 8 && width != 16 && width != 32)
        return;

    uint8_t buffer[40];
    uint8_t buf_index = 0;

    for (uint8_t i = width; i > 0; i--)
    {
        buffer[buf_index++] = (num & (1U << (i - 1))) ? '1' : '0';

        if ((i - 1) % 4 == 0 && (i - 1) != 0)
        {
            buffer[buf_index++] = ' ';
        }
    }

    debugBuffer(buffer, buf_index);
}

void usartInit(void)
{
    usart2Init();
}

void usartBufferFlush(void)
{
    usart2BufferFlush();
}