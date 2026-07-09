#include "Devices/joystick.h"
#include <stm32f407xx.h>
#include "Peripherals/adc.h"
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Logger/logger.h"
#include <stdio.h>

// Button bits (within s_btn_data). This bit order matches the field order of the
// InputButtonState members in InputState, so joystickPollFrame unpacks bit i into
// the i-th button field.
typedef enum JoystickBtnBit
{
    JoystickBtnBitRUp = 0U,
    JoystickBtnBitRRight = 1U,
    JoystickBtnBitRDown = 2U,
    JoystickBtnBitRLeft = 3U,
    JoystickBtnBitLUp = 4U,
    JoystickBtnBitLRight = 5U,
    JoystickBtnBitLDown = 6U,
    JoystickBtnBitLLeft = 7U,
    JoystickBtnBitSpecial1 = 8U,
    JoystickBtnBitSpecial2 = 9U,
} JoystickBtnBit;

#define BTN_COUNT 10U
#define BTN_MASK_ALL ((1U << BTN_COUNT) - 1U) /* bits 0..9 */

#define ANALOG_COUNT 4U
#define DEBOUNCE_MS 5U

/* Raw-axis conditioning (joystickPollFrame): the 12-bit ADC reads ~0..4095 with
 * the rest position near mid-scale. We center on ADC_CENTER, kill a small deadzone
 * around it, then scale the usable travel to +/-AXIS_OUT_MAX so a caller gets a
 * clean signed value with no per-axis calibration. The wiring inverts the axes
 * (a low ADC code is a positive/up-right push), so up and right read positive. */
#define ADC_CENTER 2048
#define AXIS_DEADZONE 150
#define AXIS_OUT_MAX 512

static volatile uint32_t s_btn_data = 0U;
static uint8_t s_btn_raw[BTN_COUNT];
static uint32_t s_btn_debounce_timer[BTN_COUNT];

/* Per-frame input snapshot (see joystickPollFrame / joystickGetState). */
static uint16_t s_prev_held = 0U;
static InputState s_frame_state;

static volatile uint16_t *s_buffer_addr = 0U;
static bool s_initialized = false;

/* Gather the 10 button GPIOs into a 10-bit active-high mask (pins are active-low
 * with pull-ups; already inverted here). Bit layout matches the JOYSTICK_DATA_*_BTN_*
 * positions used below: 0=R_Up 1=R_Right 2=R_Down 3=R_Left 4=L_Up 5=L_Right 6=L_Down
 * 7=L_Left 8=Special1 9=Special2. Owned by the joystick driver (which defines the
 * layout), not the generic pin setup. */
static uint16_t readRawButtons(void)
{
    const uint32_t a = ~GPIOA->IDR;
    const uint32_t b = ~GPIOB->IDR;
    return (uint16_t)(
        (((a >> 0U) & 1U) << 0U) |   // PA0  -> R_Up
        (((a >> 1U) & 1U) << 1U) |   // PA1  -> R_Right
        (((a >> 5U) & 1U) << 2U) |   // PA5  -> R_Down
        (((a >> 6U) & 1U) << 3U) |   // PA6  -> R_Left
        (((a >> 7U) & 1U) << 4U) |   // PA7  -> L_Up
        (((a >> 8U) & 1U) << 5U) |   // PA8  -> L_Right
        (((a >> 11U) & 1U) << 6U) |  // PA11 -> L_Down
        (((a >> 12U) & 1U) << 7U) |  // PA12 -> L_Left
        (((b >> 11U) & 1U) << 8U) |  // PB11 -> Special1
        (((b >> 12U) & 1U) << 9U));  // PB12 -> Special2
}

/* Debounce the raw button GPIOs into s_btn_data. Runs in the TIM7 poll ISR. */
static void readButtons(void)
{
    const uint32_t raw_all = readRawButtons();
    for (uint8_t i = 0U; i < BTN_COUNT; ++i)
    {
        const uint8_t raw_bit = (raw_all >> i) & 1U;
        if (raw_bit != s_btn_raw[i])
        {
            s_btn_raw[i] = raw_bit;
            s_btn_debounce_timer[i] = getSysTime();
        }
        else if (getSysTime() - s_btn_debounce_timer[i] >= DEBOUNCE_MS)
        {
            if (raw_bit)
                s_btn_data |= (1U << i);
            else
                s_btn_data &= ~(1U << i);
        }
    }
}

void joystickReadData(void)
{
    if (!s_initialized)
    {
        return;
    }

    readButtons();
}

void joystickInit(void)
{
    s_btn_data = 0U;
    s_buffer_addr = getAdc1BufferAddress();

    const uint32_t now = getSysTime();
    for (uint8_t i = 0U; i < BTN_COUNT; ++i)
    {
        s_btn_raw[i] = 0U;
        s_btn_debounce_timer[i] = now;
    }
    s_prev_held = 0U;
    s_frame_state = (InputState){0};
    s_initialized = true;
    LOGGER_LOG_INFO(LOGGER_JOYSTICK, "init: %u buttons, %u analog axes", (unsigned)BTN_COUNT, (unsigned)ANALOG_COUNT);
}

/* Center, deadzone, and scale one 12-bit ADC axis to -AXIS_OUT_MAX..+AXIS_OUT_MAX.
 * The axis is inverted (a low ADC code is a positive push), so up/right = +. */
static int16_t axisScaled(uint16_t raw)
{
    const int32_t centered = ADC_CENTER - (int32_t)raw;
    int32_t mag = (centered < 0) ? -centered : centered;
    if (mag <= AXIS_DEADZONE)
    {
        return 0;
    }
    mag -= AXIS_DEADZONE;
    const int32_t span = ADC_CENTER - AXIS_DEADZONE; /* max magnitude past the deadzone */
    int32_t scaled = (mag * AXIS_OUT_MAX + span / 2) / span;
    if (scaled > AXIS_OUT_MAX)
    {
        scaled = AXIS_OUT_MAX;
    }
    return (int16_t)((centered < 0) ? -scaled : scaled);
}

/* Fill one button's frame flags from the held/pressed/released masks at `bit`. */
static void unpackButton(InputButtonState *b, uint16_t bit,
                         uint16_t held, uint16_t pressed, uint16_t released)
{
    b->held = (held & bit) != 0U;
    b->pressed = (pressed & bit) != 0U;
    b->released = (released & bit) != 0U;
}

void joystickPollFrame(void)
{
    if (!s_initialized)
    {
        return;
    }

    /* Buttons: snapshot the debounced state and derive edges vs the previous latch.
     * The 32-bit read is atomic against the 1 ms poll ISR that writes it. Masks are
     * unpacked into per-button fields below (bit order == field order). */
    const uint16_t held = (uint16_t)(s_btn_data & BTN_MASK_ALL);
    const uint16_t pressed = (uint16_t)(held & ~s_prev_held);
    const uint16_t released = (uint16_t)(~held & s_prev_held);
    s_prev_held = held;

    unpackButton(&s_frame_state.r_up, (1U << JoystickBtnBitRUp), held, pressed, released);
    unpackButton(&s_frame_state.r_right, (1U << JoystickBtnBitRRight), held, pressed, released);
    unpackButton(&s_frame_state.r_down, (1U << JoystickBtnBitRDown), held, pressed, released);
    unpackButton(&s_frame_state.r_left, (1U << JoystickBtnBitRLeft), held, pressed, released);
    unpackButton(&s_frame_state.l_up, (1U << JoystickBtnBitLUp), held, pressed, released);
    unpackButton(&s_frame_state.l_right, (1U << JoystickBtnBitLRight), held, pressed, released);
    unpackButton(&s_frame_state.l_down, (1U << JoystickBtnBitLDown), held, pressed, released);
    unpackButton(&s_frame_state.l_left, (1U << JoystickBtnBitLLeft), held, pressed, released);
    unpackButton(&s_frame_state.special1, (1U << JoystickBtnBitSpecial1), held, pressed, released);
    unpackButton(&s_frame_state.special2, (1U << JoystickBtnBitSpecial2), held, pressed, released);

    /* Axes: sample straight from the ADC DMA buffer (each 16-bit read is atomic).
     * Buffer order matches the ADC channel sequence: LX, LY, RX, RY. */
    s_frame_state.left_x = axisScaled(s_buffer_addr[0]);
    s_frame_state.left_y = axisScaled(s_buffer_addr[1]);
    s_frame_state.right_x = axisScaled(s_buffer_addr[2]);
    s_frame_state.right_y = axisScaled(s_buffer_addr[3]);
}

void joystickGetState(InputState *out)
{
    if (out != NULL)
    {
        *out = s_frame_state;
    }
}
