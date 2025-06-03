#include "timer.h"
#include "joystick.h"
#include "buzzer.h"
#include "stm32f407xx.h"

void TIM6_DAC_IRQHandler()
{
    if (TIM6->SR & TIM_SR_UIF)
    {
        // clear flag
        TIM6->SR &= ~TIM_SR_UIF;
        buzzerInterruptHandler();
    }
}

void TIM7_IRQHandler()
{
    if (TIM7->SR & TIM_SR_UIF)
    {
        // clear flag
        TIM7->SR &= ~TIM_SR_UIF;
        joystickReadData();
    }
}

static void timer6Init(const uint32_t sample_rate_hz)
{
    const uint32_t ARR_COUNT = 100U;
    TIM6->PSC = ((84000000U / sample_rate_hz) / ARR_COUNT) - 1U;
    TIM6->ARR = ARR_COUNT - 1U;

    // TRGO to update event
    // MMS = 010: update event -> TRGO
    TIM6->CR2 &= ~TIM_CR2_MMS;
    TIM6->CR2 |= TIM_CR2_MMS_1;

    // enable update interrupt
    TIM6->DIER |= TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);

    TIM6->CR1 |= TIM_CR1_CEN;
}

static void timer7Init(uint16_t time_ms)
{
    // timer frequency = fCK_PSC / (PSC[15:0] + 1).
    // 84MHz on APB1 timer bus
    // 1ms timer resolution PSC = 839
    TIM7->PSC = 839U;

    // ARR = (desired timer / period of PSC) -1
    // (50ms/1ms)-1 = 50-1 =49
    // if time_ms = 0 we can't subtract 1 because we would overflow the counter
    TIM7->ARR = (time_ms != 0U ? (time_ms - 1U) : 0U);

    // Enable interrupt
    TIM7->DIER |= TIM_DIER_UIE;

    // enable counter
    TIM7->CR1 |= TIM_CR1_CEN;

    // timer 7 lowest priority -> just updating user inputs
    NVIC_EnableIRQ(TIM7_IRQn);
    NVIC_SetPriority(TIM7_IRQn, 15);
}

void timerInit(void)
{
    // timer 6 - frequency of 8KHz
    timer6Init(8000U);
    // timer 7 - period of 50ms
    timer7Init(50U);
}