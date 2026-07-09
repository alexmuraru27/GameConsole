#include "Devices/backlight.h"
#include "stm32f407xx.h"
#include "Logger/logger.h"

/*
 * Backlight PWM on PA3 / TIM9_CH2. TIM9 runs off the 168 MHz APB2 timer clock
 * (APB2 is /2, so its timers see 2x PCLK2). PSC=167 gives a 1 MHz counter and
 * ARR=999 a 1 kHz PWM period — well above any visible flicker — with a full
 * 1000-count duty resolution. The output is active-high (CC2P=0) and PWM mode 1
 * holds PA3 high while CNT < CCR2, so CCR2 is the on-time: duty = CCR2 / (ARR+1).
 */
#define BL_TIM_PSC 167U
#define BL_TIM_ARR 999U
#define BL_TIM_PERIOD (BL_TIM_ARR + 1U) /* full-scale duty count */

static uint8_t s_percent = BACKLIGHT_DEFAULT_PERCENT;

/* Clamp to [MIN,MAX] and snap to the nearest BACKLIGHT_STEP_PERCENT. */
static uint8_t normalizePercent(uint8_t percent)
{
    uint32_t p = ((uint32_t)percent + BACKLIGHT_STEP_PERCENT / 2U) / BACKLIGHT_STEP_PERCENT;
    p *= BACKLIGHT_STEP_PERCENT;
    if (p < BACKLIGHT_MIN_PERCENT)
    {
        p = BACKLIGHT_MIN_PERCENT;
    }
    else if (p > BACKLIGHT_MAX_PERCENT)
    {
        p = BACKLIGHT_MAX_PERCENT;
    }
    return (uint8_t)p;
}

/* Program the duty for `percent`. At 100% the compare exceeds ARR, so the output
 * stays high for the whole period (a true fully-on, with no 1-count dropout). */
static void applyDuty(uint8_t percent)
{
    TIM9->CCR2 = ((uint32_t)percent * BL_TIM_PERIOD) / 100U;
}

void backlightInit(void)
{
    /* PA3 is muxed to AF3 (TIM9_CH2) in gpioInit; here we own the timer. */
    TIM9->CR1 = 0U;
    TIM9->PSC = BL_TIM_PSC;
    TIM9->ARR = BL_TIM_ARR;

    /* Channel 2: PWM mode 1 (OC2M = 0b110) with output preload enabled. */
    TIM9->CCMR1 = (TIM9->CCMR1 & ~TIM_CCMR1_OC2M) |
                  (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1) | TIM_CCMR1_OC2PE;
    TIM9->CCER = (TIM9->CCER & ~TIM_CCER_CC2P) | TIM_CCER_CC2E; /* active-high, output on */

    applyDuty(s_percent);

    TIM9->EGR = TIM_EGR_UG;                 /* latch PSC/ARR/CCR preloads before enabling */
    TIM9->CR1 |= TIM_CR1_ARPE | TIM_CR1_CEN;

    LOGGER_LOG_INFO(LOGGER_DISPLAY, "backlight PWM up: TIM9_CH2 @ %u%%", (unsigned)s_percent);
}

void backlightSetBrightness(uint8_t percent)
{
    s_percent = normalizePercent(percent);
    applyDuty(s_percent);
    LOGGER_LOG_DEBUG(LOGGER_DISPLAY, "backlight -> %u%%", (unsigned)s_percent);
}

uint8_t backlightGetBrightness(void)
{
    return s_percent;
}
