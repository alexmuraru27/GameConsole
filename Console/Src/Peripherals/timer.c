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

void timer3Disable(void)
{
    // Stop the counter, then force the output compare to its inactive (low) level.
    // Just clearing CEN freezes the counter and leaves PB5 driven at whatever the last
    // comparison produced — which can be a constant 3.3V. The buzzer pin is AF push-pull,
    // so that DC bias keeps current flowing through the passive buzzer and heats it up.
    // OC2M = 0b100 (force inactive) drives OC2REF low independent of the counter, so with
    // active-high polarity (CC2P = 0) PB5 is actively held at 0V while idle.
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC2M) | TIM_CCMR1_OC2M_2;
}

void timer3Trigger(uint32_t frequency_hz)
{
    // calculate PWM period for frequency: cycles = (84000000U / frequency_hz )
    // cycles per period
    uint32_t arr = 84000000U / frequency_hz;
    // ensure valid period (arr should be at least 2)
    if (arr < 2)
        arr = 2;
    // adjust prescaler for low frequencies to improve precision
    uint32_t psc = 0;
    while (arr > 65535)
    {
        // TIM3 ARR is 16-bit
        psc++;
        arr = 84000000U / (frequency_hz * (psc + 1));
    }

    TIM3->PSC = psc;
    TIM3->ARR = arr - 1U;
    TIM3->CCR2 = arr / 2U; // 50% duty (square wave)
    // Restore PWM mode 1 (timer3Disable forces the output to inactive/low when idle)
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC2M) | (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1);
    TIM3->CR1 |= TIM_CR1_CEN;
}

static void timer3Init()
{
    // Defaults to 1ms
    TIM3->CR1 = 0U;                                                                        // Reset control register
    TIM3->PSC = (84U - 1);                                                                 // Prescaler = 1 (84 MHz clock)
    TIM3->ARR = 1000U;                                                                     // Default period
    TIM3->CCR2 = 500U;                                                                     // 50% duty cycle for Channel 2
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC2M) | (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1); // PWM mode 1 for Channel 2
    TIM3->CCER |= TIM_CCER_CC2E;                                                           // Enable channel 2 output
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
    timer3Init();
    // timer 6 -  period of 1ms
    timer6Init();
    // timer 7 - joystick poll, 10ms
    timer7Init(10U);
    LOGGER_LOG_DEBUG(LOGGER_CORE, "timers init: TIM3 PWM (buzzer), TIM6 1ms, TIM7 10ms");
}