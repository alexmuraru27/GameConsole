#include "usart.h"
#include <stm32f407xx.h>
#include "sysclock.h"
#include "watchdog.h"
#include "logger.h"

/* USART1 lives on APB2; its clock is PCLK2 (see the clock tree in sysclock.h). */
#define USART1_PCLK_HZ PCLK2_HZ

/*
 * USART1 (ESP-01 link) driven over DMA2:
 *   - RX: a continuously-running CIRCULAR DMA (Stream 2, channel 4) drains
 *     USART1->DR into s_rx_ring the instant each byte lands, with no CPU
 *     involvement. The reader (usartReadByte) consumes from the ring, deriving
 *     the DMA write position from NDTR. This is what makes high baud safe: even
 *     if an ISR (buzzer 1 ms / joystick 50 ms) stalls the consumer for several
 *     byte-times, the hardware keeps depositing bytes, so nothing overruns the
 *     single DR register the way a polled reader would.
 *   - TX: a per-transfer DMA (Stream 7, channel 4) streams a frame out. The
 *     blocking usartWriteBytes() waits for the shift register to empty (USART TC)
 *     before returning; usartWriteBytesStart()/usartTxWait() split that wait so a
 *     caller can overlap the drain with other work (the per-frame ESP-NOW poll
 *     arms the send, runs the game's render, then collects the reply).
 * The byte-oriented API is unchanged, so network.c and the ESP flasher are
 * agnostic to the DMA backing.
 */

/* Power-of-two ring: ~22 ms of wire time at 921k baud — far more than the worst
 * ISR stall or inter-frame burst, with cheap masked wraparound. */
#define RX_RING_SIZE 2048U
#define RX_RING_MASK (RX_RING_SIZE - 1U)

static uint8_t s_rx_ring[RX_RING_SIZE];
static uint16_t s_rx_tail; /* next byte the consumer will read */
static uint32_t s_baud;    /* last programmed baud, so re-asserts don't re-log */

/* DMA write position in the ring: the circular stream counts NDTR down from
 * RX_RING_SIZE, so the next slot it will fill is (RX_RING_SIZE - NDTR). */
static inline uint16_t usartRxHead(void)
{
    return (uint16_t)((RX_RING_SIZE - (uint16_t)(DMA2_Stream2->NDTR & 0xFFFFU)) & RX_RING_MASK);
}

/* (Re)arm the circular RX DMA from a clean state. Safe to call repeatedly. */
static void usartRxDmaStart(void)
{
    DMA2_Stream2->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream2->CR & DMA_SxCR_EN)
    {
    }
    DMA2->LIFCR = DMA_LIFCR_CTCIF2 | DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTEIF2 |
                  DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2;

    DMA2_Stream2->PAR = (uint32_t)&USART1->DR;
    DMA2_Stream2->M0AR = (uint32_t)s_rx_ring;
    DMA2_Stream2->NDTR = RX_RING_SIZE;
    DMA2_Stream2->CR = (4U << DMA_SxCR_CHSEL_Pos) | /* channel 4 = USART1_RX        */
                       DMA_SxCR_MINC |              /* increment through the ring    */
                       DMA_SxCR_CIRC |              /* wrap forever                  */
                       DMA_SxCR_PL_1;               /* high priority                 */
    /* DIR=00 (periph->mem), PSIZE=MSIZE=00 (byte), PINC=0: all defaults. */

    s_rx_tail = 0U;
    DMA2_Stream2->CR |= DMA_SxCR_EN;
}

void usartSetBaud(uint32_t baud)
{
    /* OVER8 = 0 (16x oversampling): the BRR register holds PCLK/baud directly,
     * encoded as 12-bit mantissa + 4-bit fraction. Round to the nearest. */
    USART1->BRR = (USART1_PCLK_HZ + (baud / 2U)) / baud;

    /* The runtime re-asserts the baud on every transaction; only log real
     * changes (sync probing, the flasher's rate switch) so the line isn't spammed. */
    if (baud != s_baud)
    {
        LOGGER_LOG_DEBUG(LOGGER_USART, "baud -> %u (BRR=%u)", (unsigned)baud, (unsigned)USART1->BRR);
        s_baud = baud;
    }
}

void usartInit(void)
{
    /* USART1 clock is enabled centrally in peripheralsClockEnable(); enable DMA2
     * here (idempotent — ADC1 and the FSMC display also run on DMA2). */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    /* Disable while (re)configuring. */
    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;

    usartSetBaud(USART1_DEFAULT_BAUD);

    /* Start the RX ring before enabling the receiver, then route RX and TX
     * through DMA (DMAR/DMAT request DMA service on RXNE/TXE respectively). */
    usartRxDmaStart();
    USART1->CR3 = USART_CR3_DMAR | USART_CR3_DMAT;

    /* 8 data bits, no parity (CR1 cleared above), enable TX, RX and the peripheral. */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    LOGGER_LOG_INFO(LOGGER_USART, "USART1 up @ %u baud, DMA rx(S2)/tx(S7)", (unsigned)USART1_DEFAULT_BAUD);
}

/* True between a usartWriteBytesStart() and the drain that completes it. */
static volatile bool s_tx_in_flight;

/* Arm + enable the TX DMA stream for `data`/`len`. Returns false if a previous
 * transfer left the stream stuck enabled past `deadline`. */
static bool txArmStream(const uint8_t *data, uint16_t len, uint32_t deadline)
{
    DMA2_Stream7->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream7->CR & DMA_SxCR_EN)
    {
        if (getSysTime() >= deadline)
        {
            LOGGER_LOG_ERROR(LOGGER_USART, "tx stream stuck enabled (prev transfer hung)");
            return false;
        }
    }
    DMA2->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7 |
                  DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;

    DMA2_Stream7->PAR = (uint32_t)&USART1->DR;
    DMA2_Stream7->M0AR = (uint32_t)data;
    DMA2_Stream7->NDTR = len;
    DMA2_Stream7->CR = (4U << DMA_SxCR_CHSEL_Pos) | /* channel 4 = USART1_TX        */
                       DMA_SxCR_DIR_0 |             /* memory-to-peripheral          */
                       DMA_SxCR_MINC |              /* increment through the buffer   */
                       DMA_SxCR_PL_1;               /* high priority                 */

    /* Clear TC so the drain sees this frame's completion, not a stale one. */
    USART1->SR &= ~USART_SR_TC;
    DMA2_Stream7->CR |= DMA_SxCR_EN;
    return true;
}

/* Block until the armed transfer has fully drained: DMA has handed every byte to
 * the USART, then the shift register has emptied (TC) — so a following reset/boot
 * pin toggle doesn't truncate the frame. False on DMA error or timeout. */
static bool txDrain(uint32_t deadline)
{
    while ((DMA2->HISR & (DMA_HISR_TCIF7 | DMA_HISR_TEIF7)) == 0U)
    {
        if (getSysTime() >= deadline)
        {
            LOGGER_LOG_ERROR(LOGGER_USART, "tx DMA timeout: %u byte(s) unsent",
                             (unsigned)(DMA2_Stream7->NDTR & 0xFFFFU));
            DMA2_Stream7->CR &= ~DMA_SxCR_EN;
            return false;
        }
    }
    if (DMA2->HISR & DMA_HISR_TEIF7)
    {
        LOGGER_LOG_ERROR(LOGGER_USART, "tx DMA transfer error (bus fault on TX stream)");
        DMA2_Stream7->CR &= ~DMA_SxCR_EN;
        return false;
    }
    DMA2_Stream7->CR &= ~DMA_SxCR_EN;

    while ((USART1->SR & USART_SR_TC) == 0U)
    {
        if (getSysTime() >= deadline)
        {
            LOGGER_LOG_ERROR(LOGGER_USART, "tx shift-register (TC) timeout");
            return false;
        }
    }
    return true;
}

bool usartWriteBytesStart(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (len == 0U)
    {
        return true;
    }
    const uint32_t deadline = getSysTime() + timeout_ms;

    /* Drain any still-in-flight prior transfer before reprogramming the stream:
     * this is where the async pattern actually blocks — and only if the previous
     * frame somehow hasn't finished draining yet (normally it has, overlapped with
     * the caller's work), so it protects the prior buffer at effectively no cost. */
    if (s_tx_in_flight)
    {
        if (!txDrain(deadline))
        {
            s_tx_in_flight = false;
            return false;
        }
        s_tx_in_flight = false;
    }

    if (!txArmStream(data, len, deadline))
    {
        return false;
    }
    s_tx_in_flight = true;
    return true;
}

bool usartTxWait(uint32_t timeout_ms)
{
    if (!s_tx_in_flight)
    {
        return true;
    }
    const bool ok = txDrain(getSysTime() + timeout_ms);
    s_tx_in_flight = false;
    return ok;
}

bool usartWriteBytes(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    /* Blocking = arm the transfer then wait it out. */
    if (!usartWriteBytesStart(data, len, timeout_ms))
    {
        return false;
    }
    return usartTxWait(timeout_ms);
}

void usartFlushRx(void)
{
    /* This runs at transaction boundaries (npBegin / sync probing), not in the
     * per-byte hot path, so it's the safe place to surface RX trouble:
     *   - ORE: a byte arrived before the DMA serviced the previous one (an actual
     *     RX overrun — the thing that can bite at high baud); the byte is lost and
     *     the frame will fail CRC and be retried. We don't read DR to clear it
     *     (the DMA owns DR); it self-clears on the DMA's next read.
     *   - DMA stream error: the RX DMA itself faulted (bus / direct-mode error),
     *     which would stall reception — log and clear the flags.
     * Both are exactly the "rx transmission issue" we want visibility on. */
    if (USART1->SR & USART_SR_ORE)
    {
        LOGGER_LOG_WARN(LOGGER_USART, "rx overrun (ORE): byte(s) lost — frame fails crc + retries");
    }
    const uint32_t rx_dma_err = DMA2->LISR & (DMA_LISR_TEIF2 | DMA_LISR_DMEIF2 | DMA_LISR_FEIF2);
    if (rx_dma_err != 0U)
    {
        LOGGER_LOG_ERROR(LOGGER_USART, "rx DMA error (LISR 0x%lX) — reception may have stalled",
                         (unsigned long)rx_dma_err);
        DMA2->LIFCR = DMA_LIFCR_CTEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2;
    }

    /* Drop whatever the DMA has buffered so a transaction starts from a clean
     * line after a baud change or desync — just catch the tail up to the head. */
    s_rx_tail = usartRxHead();
}

int usartReadByte(uint32_t timeout_ms)
{
    const uint32_t deadline = getSysTime() + timeout_ms;

    while (s_rx_tail == usartRxHead())
    {
        /* This is the single byte-wait every ESP exchange funnels through, and a
         * sanctioned one can legitimately block for seconds (a WiFi connect waits
         * up to ~16 s on the first reply byte). Feed the watchdog here so those
         * long-but-bounded waits don't look like a wedge — the loop still exits at
         * its own deadline, so a truly stuck console (which never reaches here) is
         * still caught. */
        watchdogKick();
        if (getSysTime() >= deadline)
        {
            return -1;
        }
    }

    const uint8_t b = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) & RX_RING_MASK);
    return (int)b;
}
