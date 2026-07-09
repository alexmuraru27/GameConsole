#include "timer.h"
#include "joystick.h"
#include "buzzer.h"
#include "stm32f407xx.h"
#include "logger.h"

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

static void timer6Init()
{
    // (1 µs tick)
    TIM6->PSC = (84U - 1);
    // 1000 ticks = 1 ms
    TIM6->ARR = 1000U;
    // Enable update interrupt
    TIM6->DIER |= TIM_DIER_UIE;
    TIM6->CR1 |= TIM_CR1_CEN;
    // High prio - sound
    NVIC_SetPriority(TIM6_DAC_IRQn, 1);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

static void timer7Init(uint16_t time_ms)
{
    // TIM7 runs off the 84 MHz APB1 timer clock. Prescale to a 100 us tick
    // (84 MHz / 8400 = 10 kHz), so a period in milliseconds maps to
    // ARR = time_ms * 10 - 1, and the 16-bit ARR still spans up to ~6.5 s.
    // (The previous PSC=839 was a 10 us tick, NOT the 1 ms its comment claimed,
    // so ARR=time_ms-1 fired the ISR 100x too fast — every 0.5 ms, not 50 ms.)
    TIM7->PSC = (8400U - 1U);

    const uint32_t ticks = (uint32_t)time_ms * 10U; // 100 us ticks per millisecond
    TIM7->ARR = (uint16_t)(ticks != 0U ? ticks - 1U : 0U);

    // Enable interrupt
    TIM7->DIER |= TIM_DIER_UIE;

    // enable counter
    TIM7->CR1 |= TIM_CR1_CEN;

    // Joystick polling is the least-urgent ISR, but it must still outrank SVC/PendSV
    // (priority 15) so it can preempt a syscall running on the kernel stack — SVC and
    // PendSV are kept strictly lowest. 14 = just above them.
    NVIC_EnableIRQ(TIM7_IRQn);
    NVIC_SetPriority(TIM7_IRQn, 14);
}

void timerInit(void)
{
    // timer 6 -  period of 1ms (buzzer note advance)
    timer6Init();
    // timer 7 - joystick poll, 10ms
    timer7Init(10U);
    LOGGER_LOG_DEBUG(LOGGER_CORE, "timers init: TIM6 1ms, TIM7 10ms (TIM3 PWM owned by buzzer)");
}