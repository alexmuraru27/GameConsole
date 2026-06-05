#include "stm32f4xx_it.h"
#include <stdio.h>
#include <stm32f407xx.h>

void NMI_Handler(void)
{

  while (1)
  {
  }
}

void hard_fault_dump(uint32_t *stack)
{
    printf("=== HARD FAULT ===\n");
    printf("PC  =0x%08lX\n", stack[6]);
    printf("LR  =0x%08lX\n", stack[5]);
    printf("PSR =0x%08lX\n", stack[7]);
    printf("HFSR=0x%08lX\n", SCB->HFSR);
    printf("CFSR=0x%08lX\n", SCB->CFSR);
    if (SCB->CFSR & SCB_CFSR_MMARVALID_Msk)
        printf("MMFAR=0x%08lX\n", SCB->MMFAR);
    if (SCB->CFSR & SCB_CFSR_BFARVALID_Msk)
        printf("BFAR=0x%08lX\n", SCB->BFAR);
    while (1) {}
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b hard_fault_dump \n"
    );
}

void MemManage_Handler(void)
{

  while (1)
  {
  }
}

void BusFault_Handler(void)
{

  while (1)
  {
  }
}

void UsageFault_Handler(void)
{

  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
