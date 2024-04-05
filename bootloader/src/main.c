#include <inttypes.h>
#include "stm32f407xx.h"

static void start_app(uint32_t *pc, uint32_t *sp)
{
    __asm("           \n\
          msr msp, r1 \n\
          bx r0       \n\
    ");
}

extern uint32_t *__approm_start__[];
int main()
{
    uint32_t *app_sp = __approm_start__[0];
    uint32_t *app_start_instruction = __approm_start__[1];
    __disable_irq();
    start_app(app_start_instruction, app_sp);
    while (1)
    {
    }
    return 0;
}