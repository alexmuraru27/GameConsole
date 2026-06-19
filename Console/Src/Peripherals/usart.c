#include "usart.h"
#include <stm32f407xx.h>
#include "sysclock.h"
#include "logger.h"

/* USART1 lives on APB2; PCLK2 is SYSCLK/2 = 84 MHz (see systemClockConfig). */
#define USART1_PCLK_HZ 84000000U

/*
 * Polled 8N1. At the runtime/flash baud of 115200 there is ~86 us of slack per
 * byte — far longer than any ISR — so the polled reader keeps up without
 * overrunning. (A DMA-RX ring was tried for higher baud; reverted in favour of
 * the simpler, proven polled path since the link runs at 115200.)
 */

void usartSetBaud(uint32_t baud)
{
    /* OVER8 = 0 (16x oversampling): the BRR register holds PCLK/baud directly,
     * encoded as 12-bit mantissa + 4-bit fraction. Round to the nearest. */
    USART1->BRR = (USART1_PCLK_HZ + (baud / 2U)) / baud;
}

void usartInit(void)
{
    /* The USART1 clock is enabled centrally in peripheralsClockEnable(). */

    /* Disable while (re)configuring. */
    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;

    usartSetBaud(USART1_DEFAULT_BAUD);

    /* 8 data bits, no parity (CR1 cleared above), enable TX, RX and the peripheral. */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    LOGGER_LOG_INFO(LOGGER_FLASHER, "USART1 up @ %u baud (ESP-01)", (unsigned)USART1_DEFAULT_BAUD);
}

bool usartWriteBytes(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    const uint32_t deadline = getSysTime() + timeout_ms;

    for (uint16_t i = 0U; i < len; i++)
    {
        while ((USART1->SR & USART_SR_TXE) == 0U)
        {
            if (getSysTime() >= deadline)
            {
                return false;
            }
        }
        USART1->DR = data[i];
    }

    /* Wait for the last byte to leave the shift register so a following
     * reset/boot pin toggle doesn't truncate the frame. */
    while ((USART1->SR & USART_SR_TC) == 0U)
    {
        if (getSysTime() >= deadline)
        {
            return false;
        }
    }

    return true;
}

void usartFlushRx(void)
{
    /* Reading SR then DR clears RXNE and any overrun (ORE), discarding pending
     * bytes — used after a baud change or a desync so a stale byte isn't misread.
     * Drain the whole hardware buffer (DR + shift register), not just one byte. */
    while ((USART1->SR & (USART_SR_RXNE | USART_SR_ORE)) != 0U)
    {
        (void)USART1->SR;
        (void)USART1->DR;
    }
}

int usartReadByte(uint32_t timeout_ms)
{
    const uint32_t deadline = getSysTime() + timeout_ms;

    while ((USART1->SR & USART_SR_RXNE) == 0U)
    {
        if (getSysTime() >= deadline)
        {
            return -1;
        }
    }

    return (int)(USART1->DR & 0xFFU);
}
