#include "usart.h"
#include <stdbool.h>
#include <stm32f407xx.h>
#include <string.h>

#define USART_BUFFER_SIZE ((uint32_t)1024U)

uint8_t s_usart2_circular_buffer[USART_BUFFER_SIZE] = {0U};
volatile uint16_t s_usart2_head = 0;
volatile uint16_t s_usart2_tail = 0;
volatile uint16_t s_usart2_dma_length = 0;
volatile uint8_t s_usart2_dma_busy = 0;

static inline uint16_t circularBufferSize(void)
{
    // actually this local vars have a purpose
    // since s_usart2 head and tail are volatile
    // we snapshot the values
    uint16_t head = s_usart2_head;
    uint16_t tail = s_usart2_tail;

    if (head >= tail)
    {
        return head - tail;
    }
    else
    {
        return USART_BUFFER_SIZE - tail + head;
    }
}

static inline uint16_t circularBufferFree(void)
{
    return USART_BUFFER_SIZE - circularBufferSize() - 1U;
}

static inline uint16_t circularBufferContiguousSize(void)
{
    uint16_t head = s_usart2_head;
    uint16_t tail = s_usart2_tail;

    if (head >= tail)
    {
        return head - tail;
    }
    else
    {
        // data wraps around, return size from tail to end of buffer
        return USART_BUFFER_SIZE - tail;
    }
}

static void startDmaTransfer(void)
{
    // check if there's data to transmit
    uint16_t size = circularBufferContiguousSize();
    if (size == 0)
    {
        return;
    }

    // disable DMA before configuration
    DMA1_Stream6->CR &= ~DMA_SxCR_EN;

    // wait for DMA to be actually disabled
    while (DMA1_Stream6->CR & DMA_SxCR_EN)
    {
        // wait
    }

    // clear all DMA flags for Stream 6
    DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 |
                  DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;

    // configure DMA transfer
    DMA1_Stream6->M0AR = (uint32_t)&s_usart2_circular_buffer[s_usart2_tail];
    DMA1_Stream6->NDTR = size;

    // remember transfer length for tail update after completion
    s_usart2_dma_length = size;
    s_usart2_dma_busy = 1;

    // start DMA transfer
    DMA1_Stream6->CR |= DMA_SxCR_EN;
}

void usart2SetDmaFree(void)
{
    // update tail
    if (s_usart2_dma_length > 0)
    {
        s_usart2_tail = (s_usart2_tail + s_usart2_dma_length) % USART_BUFFER_SIZE;
        s_usart2_dma_length = 0;
    }

    s_usart2_dma_busy = 0;

    // start next transfer if data is available
    startDmaTransfer();
}

static void usart2BufferFlush(void)
{
    // if DMA is busy, it will automatically continue in usart2SetDmaFree
    if (s_usart2_dma_busy)
    {
        return;
    }

    // try to start transmission
    startDmaTransfer();
}

static void usart2Send(const uint8_t *data, uint16_t size)
{
    // disable interrupts to protect buffer update
    NVIC_DisableIRQ(DMA1_Stream6_IRQn);

    // check if there's enough space
    uint16_t free_space = circularBufferFree();
    if (size > free_space)
    {
        // truncate
        size = free_space;
    }

    // copy data to circular buffer
    uint16_t head = s_usart2_head;
    uint16_t space_to_end = USART_BUFFER_SIZE - head;

    if (size <= space_to_end)
    {
        // data fits without wrapping
        memcpy(&s_usart2_circular_buffer[head], data, size);
        s_usart2_head = (head + size) % USART_BUFFER_SIZE;
    }
    else
    {
        // data wraps around
        memcpy(&s_usart2_circular_buffer[head], data, space_to_end);
        memcpy(&s_usart2_circular_buffer[0], &data[space_to_end], size - space_to_end);
        s_usart2_head = size - space_to_end;
    }

    // re-enable interrupts
    NVIC_EnableIRQ(DMA1_Stream6_IRQn);

    // try to always trigger a flush
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

    // send reverse order
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