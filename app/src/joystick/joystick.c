#include "joystick.h"

volatile static uint16_t axis_values[2] = {128U, 128U};
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

    // Channels(0, 1)
    ADC1->SQR3 &= ~ADC_SQR3_SQ1_Msk;
    ADC1->SQR3 |= (1 << ADC_SQR3_SQ2_Pos);

    // Nr Conversions(2)
    ADC1->SQR1 |= ADC_SQR1_L_0;

    // Continuous conversion mode
    ADC1->CR2 |= ADC_CR2_CONT;

    // Sampling Rate(480 cycles)
    ADC1->SMPR2 |= ADC_SMPR2_SMP0_2 | ADC_SMPR2_SMP0_1 | ADC_SMPR2_SMP0_0;
    ADC1->SMPR2 |= ADC_SMPR2_SMP1_2 | ADC_SMPR2_SMP1_1 | ADC_SMPR2_SMP1_0;

    // Scan mode
    ADC1->CR1 |= ADC_CR1_SCAN;

    // DMA
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    DMA2_Stream0->CR &= ~DMA_SxCR_DIR;
    DMA2_Stream0->CR |= DMA_SxCR_CIRC;
    DMA2_Stream0->CR |= DMA_SxCR_MINC;
    DMA2_Stream0->CR |= DMA_SxCR_PSIZE_0;
    DMA2_Stream0->CR |= DMA_SxCR_MSIZE_0;
    DMA2_Stream0->CR &= ~DMA_SxCR_CHSEL;
    DMA2_Stream0->PAR = (uint32_t)(&(ADC1->DR));
    DMA2_Stream0->M0AR = (uint32_t)&axis_values;
    DMA2_Stream0->NDTR = 2;
    DMA2_Stream0->CR |= DMA_SxCR_EN;

    // DMA Enable
    ADC1->CR2 |= ADC_CR2_DMA;
    ADC1->CR2 |= ADC_CR2_DDS;

    // Enable ADC
    ADC1->CR2 |= ADC_CR2_ADON;

    // Start conversion
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

static void joystick_gpio_init(void)
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

JOYSTICK_HORIZONTAL get_joystick_x(void)
{
    JOYSTICK_HORIZONTAL xVal = JOYSTICK_HORIZONTAL_CENTERED;
    if (axis_values[0U] < 100U)
    {
        xVal = JOYSTICK_HORIZONTAL_LEFT;
    }
    else if (axis_values[0U] > 156U)
    {
        xVal = JOYSTICK_HORIZONTAL_RIGHT;
    }
    return xVal;
}
JOYSTICK_VERTICAL get_joystick_y(void)
{
    JOYSTICK_HORIZONTAL yVal = JOYSTICK_VERTICAL_CENTERED;
    if (axis_values[1U] < 100U)
    {
        yVal = JOYSTICK_VERTICAL_UP;
    }
    else if (axis_values[1U] > 156U)
    {
        yVal = JOYSTICK_VERTICAL_DOWN;
    }
    return yVal;
}

JOYSTICK_SWITCH get_joystick_switch()
{
    return ((((GPIOA->IDR & GPIO_IDR_ID2) >> GPIO_IDR_ID2_Pos) != 0U)
                ? JOYSTICK_SWITCH_OFF
                : JOYSTICK_SWITCH_ON);
}