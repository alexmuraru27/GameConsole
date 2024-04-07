#include "joystick.h"

volatile static uint16_t axis_values[2];
volatile static uint8_t adc1_conversion_index = 0U;
void ADC_IRQHandler(void)
{
    if (ADC1->SR & ADC_SR_EOC)
    {
        axis_values[adc1_conversion_index] = ADC1->DR;
        adc1_conversion_index++;
        adc1_conversion_index = adc1_conversion_index % 2;
    }
}

static void joystick_adc_config(void)
{
    // ######## APB2 Clock ########
    // Enable clock for ADC1
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    // Prescale clock to APB2/8
    ADC123_COMMON->CCR |= ADC_CCR_ADCPRE_0 | ADC_CCR_ADCPRE_1;

    // ######## GPIO Analog ########
    // ADC 1 X axis
    GPIOA->MODER |= (GPIO_MODER_MODER0);
    // ADC 2 Y axis
    GPIOA->MODER |= (GPIO_MODER_MODER1);

    // Disable ADC
    ADC1->CR2 &= ~ADC_CR2_ADON;

    // Resolution set to 8 bit
    ADC1->CR1 &= ~ADC_CR1_RES_Msk;
    ADC1->CR1 |= ADC_CR1_RES_1;

    // Select the channels (0 and 1)
    ADC1->SQR3 &= ~ADC_SQR3_SQ1_Msk;
    ADC1->SQR3 |= (1 << ADC_SQR3_SQ2_Pos);

    // Set the number of conversions channels = 2
    ADC1->SQR1 |= ADC_SQR1_L_0;

    // Continuous conversion mode
    ADC1->CR2 |= ADC_CR2_CONT;

    // Generate interrupt at end of sequence
    ADC1->CR2 |= ADC_CR2_EOCS;
    ADC1->CR1 |= ADC_CR1_EOCIE;

    // Set sampling rate to 84 cycles
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP0_Msk;
    ADC1->SMPR2 |= ADC_SMPR2_SMP0_2 | ADC_SMPR2_SMP0_1 | ADC_SMPR2_SMP0_0;
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP1_Msk;
    ADC1->SMPR2 |= ADC_SMPR2_SMP1_2 | ADC_SMPR2_SMP1_1 | ADC_SMPR2_SMP1_0;

    // Enable scan mode
    ADC1->CR1 |= ADC_CR1_SCAN;
    // Enable ADC
    ADC1->CR2 |= ADC_CR2_ADON;

    // Start conversion
    __NVIC_EnableIRQ(ADC_IRQn);
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

void joystick_gpio_init(void)
{
    // switch_input
    GPIOA->MODER &= ~(GPIO_MODER_MODER2);
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD2);
    GPIOA->PUPDR |= (GPIO_PUPDR_PUPD2_0);
}

void joystick_init(void)
{
    joystick_adc_config();
    joystick_gpio_init();
}

uint16_t get_joystick_x(void)
{
    return axis_values[0U];
}
uint16_t get_joystick_y(void)
{
    return axis_values[1U];
}

uint8_t get_joystick_switch()
{
    return (GPIOA->IDR & GPIO_IDR_ID2) >> GPIO_IDR_ID2_Pos == 0U;
}