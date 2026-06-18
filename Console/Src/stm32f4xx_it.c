#include "stm32f4xx_it.h"
#include "faults.h"
#include <stdio.h>
#include <stm32f407xx.h>

void NMI_Handler(void)
{

  while (1)
  {
  }
}

/* The four fault vectors share one decoder. Each naked handler selects the
 * faulting stack (MSP if EXC_RETURN bit 2 is clear, else PSP), forwards the frame
 * pointer (r0), the EXC_RETURN value (r1), and the fault kind (r2) to
 * faultReport(), which returns the EXC_RETURN to use for a recoverable game
 * crash (and never returns for a fatal kernel fault). The push keeps the stack
 * 8-byte aligned across the call. */
#define FAULT_TRAMPOLINE(kind)        \
    __asm volatile(                   \
        "tst lr, #4        \n"        \
        "ite eq            \n"        \
        "mrseq r0, msp     \n"        \
        "mrsne r0, psp     \n"        \
        "mov r1, lr        \n"        \
        "movs r2, %0       \n"        \
        "push {r0, r1}     \n"        \
        "bl faultReport    \n"        \
        "add sp, sp, #8    \n"        \
        "bx r0             \n"        \
        : : "i"(kind))

__attribute__((naked)) void HardFault_Handler(void) { FAULT_TRAMPOLINE(FAULT_HARD); }
__attribute__((naked)) void MemManage_Handler(void) { FAULT_TRAMPOLINE(FAULT_MEMMANAGE); }
__attribute__((naked)) void BusFault_Handler(void) { FAULT_TRAMPOLINE(FAULT_BUS); }
__attribute__((naked)) void UsageFault_Handler(void) { FAULT_TRAMPOLINE(FAULT_USAGE); }

/* SVC_Handler lives in Console/Src/Kernel/syscall.c (the syscall trap). */

void DebugMon_Handler(void)
{
}

/* PendSV_Handler lives in Console/Src/Kernel/scheduler.c (switches game -> console). */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
