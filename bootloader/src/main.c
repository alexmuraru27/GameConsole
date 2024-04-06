#include <inttypes.h>
#include "stm32f407xx.h"
extern uint32_t __approm_start__;

static void branch_to_app(uint32_t reset_handler, uint32_t sp)
{
    // sp -> msp
    // reset_handler -> branching
    __asm("           \n\
          msr msp, r1 \n\
          bx r0       \n\
    ");
}

static void set_app_vtor(uint32_t *app_start_rom_addr)
{
    // Sets the The Vector Table Offset Register to the App text memory section
    SCB->VTOR = ((uint32_t)app_start_rom_addr & SCB_VTOR_TBLOFF_Msk);
}

static void start_app(uint32_t *app_rom_start_addr)
{
    uint32_t app_sp = app_rom_start_addr[0];
    uint32_t app_start_reset_handler = app_rom_start_addr[1];
    __disable_irq();
    set_app_vtor(app_rom_start_addr);
    branch_to_app(app_start_reset_handler, app_sp);
}

int main()
{
    start_app(&__approm_start__);
    while (1)
    {
    }
    return 0;
}