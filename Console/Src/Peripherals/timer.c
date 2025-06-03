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

void timer3Disable(void)
{
    TIM3->CR1 &= ~TIM_CR1_CEN;
}

void timer3Trigger(uint32_t frequency_hz, uint8_t duty)
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
    TIM3->CCR2 = (arr * duty) / 100U;
    TIM3->CR1 |= TIM_CR1_CEN;
}

static void timer3Init()
{
    TIM3->CR1 = 0U;                                                                        // Reset control register
    TIM3->PSC = 0U;                                                                        // Prescaler = 1 (84 MHz clock)
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
    timer3Init();
    // timer 6 -  period of 1ms
    timer6Init();
    // timer 7 - period of 50ms
    timer7Init(50U);
}