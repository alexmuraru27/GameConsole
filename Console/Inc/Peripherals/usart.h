#ifndef __USART_COMM_H
#define __USART_COMM_H
#include <stdint.h>
#include <stdbool.h>

/*
 * USART1 (PA9 TX / PA10 RX) — the ESP-01 link, 8N1. RX is backed by a
 * continuously-running circular DMA ring (DMA2 Stream 2) and TX by a per-transfer
 * DMA (DMA2 Stream 7), so the byte API below never loses data even at high baud
 * when an ISR briefly delays the consumer. Clients: the runtime network protocol
 * (network.c) and the ESP flasher. PCLK2 is 84 MHz.
 */

/* Default baud used by the ESP ROM bootloader for flashing. */
#define USART1_DEFAULT_BAUD 115200U

/* Enable the USART1 clock and configure it at USART1_DEFAULT_BAUD, 8N1, TX+RX.
 * Pins are configured separately in gpioInit(). Safe to call more than once. */
void usartInit(void);

/* Reprogram the baud rate (used by the flasher's change_transmission_rate). */
void usartSetBaud(uint32_t baud);

/* Send len bytes, blocking until the line drains or timeout_ms elapses.
 * Returns true on success, false on timeout. Used by paths that must not return
 * until the line is idle — the ESP flasher (pin toggles) and baud switches. */
bool usartWriteBytes(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/* Async TX: arm the DMA for `data`/`len` and return immediately, WITHOUT waiting
 * for the bytes to drain — so the caller can do useful work while the frame goes
 * out (the per-frame ESP-NOW poll overlaps it with the game's render). `data` must
 * stay valid until the transfer completes; the next Start (or a usartTxWait)
 * drains the prior transfer first, so a single reused buffer is safe as long as it
 * is not rewritten before then. A still-in-flight prior transfer is drained first
 * (bounded by timeout_ms). Returns false on timeout/DMA error. Pairs with
 * usartTxWait; usartWriteBytes is just Start + Wait. */
bool usartWriteBytesStart(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/* Block until the in-flight async TX has fully drained (DMA done + shift register
 * empty), bounded by timeout_ms. Returns true if drained (or none was pending).
 * Normally returns at once — the request drains during the overlapped work. */
bool usartTxWait(uint32_t timeout_ms);

/* Discard any buffered (unread) received bytes — used when switching baud. */
void usartFlushRx(void);

/* Read one byte, blocking up to timeout_ms. Returns the byte (0..255) or -1 on timeout. */
int usartReadByte(uint32_t timeout_ms);

#endif /* __USART_COMM_H */
