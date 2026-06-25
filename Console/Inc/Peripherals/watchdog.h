#ifndef __PERIPHERALS_WATCHDOG_H
#define __PERIPHERALS_WATCHDOG_H

/*
 * Independent watchdog (IWDG): the last-resort reset backstop. It catches the
 * hangs nothing else can unwind — an infinite loop in privileged console code, a
 * bus that wedges past its driver timeout, or a clock failure (the IWDG runs off
 * the always-on LSI, so it keeps counting even if the system clock dies). A
 * runaway *game* is caught faster and recovered gracefully by the kernel's
 * per-callback deadline (kernelGameDeadlineTick); the IWDG is for everything that
 * can't be recovered by abandoning a game context.
 *
 * Once started the IWDG cannot be stopped except by a reset, so every long path
 * must reach watchdogKick() within the timeout. It is kicked from the menu loop,
 * the kernel game loop, the long flash/download loops (a 128 KB sector erase runs
 * with interrupts masked for up to ~4 s), and — crucially — the UART byte-wait
 * (usartReadByte), which backs the multi-second WiFi scan/connect/HTTP operations
 * a console can sit in well past this timeout. With those fed, the longest
 * unguarded stretch is a single sector erase, so the timeout sits safely above it.
 */

/* Arm the IWDG (~10 s timeout). Call once, late in boot, after the subsystems
 * whose bring-up could legitimately block are up. Logs a prior watchdog reset. */
void watchdogInit(void);

/* Refresh the counter. Cheap (a single register write); safe to call before the
 * IWDG is armed (the write is simply ignored). */
void watchdogKick(void);

#endif /* __PERIPHERALS_WATCHDOG_H */
