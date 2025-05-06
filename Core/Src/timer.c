#include "timer.h"
#include "joystick.h"
#include "stm32f407xx.h"

void TIM6_DAC_IRQHandler()
{
    if (TIM6->SR & TIM_SR_UIF)
    {
        // clear flag
        TIM6->SR &= ~TIM_SR_UIF;
        joystickReadData();
    }
}

static void timer6Init(uint16_t time_ms)
{
    // timer frequency = fCK_PSC / (PSC[15:0] + 1).
    // 84MHz on APB1 timer bus
    // 1ms timer resolution PSC = 839
    TIM6->PSC = 839U;

    // ARR = (desired timer / period of PSC) -1
    // (50ms/1ms)-1 = 50-1 =49
    // if time_ms = 0 we can't subtract 1 because we would overflow the counter
    TIM6->ARR = (time_ms != 0U ? (time_ms - 1U) : 0U);

    // Enable interrupt
    TIM6->DIER |= TIM_DIER_UIE;

    // enable counter
    TIM6->CR1 |= TIM_CR1_CEN;

    // Give timer 6 lowest priority -> just updating user inputs
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 15); // Lowest priority
}

void timerInit(void)
{
    timer6Init(50U);
}