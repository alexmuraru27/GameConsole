#include "buzzer.h"
#include "dac.h"
#include <math.h>

#define DAC_BUFFER_SIZE 256
volatile uint8_t dac_buffer[DAC_BUFFER_SIZE];
volatile uint16_t dac_index = 0;

void buzzerInit(void)
{
    const float PI_2 = 2.0f * 3.14159265f;
    for (uint16_t i = 0; i < DAC_BUFFER_SIZE; i++)
    {
        const float angle = PI_2 * i / DAC_BUFFER_SIZE;
        // -1 -> 1
        const float sine = sinf(angle);
        // 0 -> 255
        dac_buffer[i] = (uint8_t)((sine + 1.0f) * 127.5f);
    }
}

void buzzerInterruptHandler(void)
{
    dacWrite(dac_buffer[dac_index++]);
    if (dac_index == DAC_BUFFER_SIZE)
    {
        dac_index = 0U;
    }
}