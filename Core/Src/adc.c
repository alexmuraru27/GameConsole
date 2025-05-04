#include "adc.h"
#include <stm32f407xx.h>

#define ADC_BUFFER_SIZE 4U
volatile uint16_t g_adc_buffer[ADC_BUFFER_SIZE] = {0U};

volatile uint16_t *getAdc1BufferAddress()
{
    // Used in DMA
    return g_adc_buffer;
}

uint8_t getAdc1BufferSize()
{
    // Used in DMA
    return ADC_BUFFER_SIZE;
}

void adcInit(void)
{
    // set ADC prescaler (ADC common control register)
    ADC->CCR |= (3U << ADC_CCR_ADCPRE_Pos); // ADCPRE = PCLK2 / 8

    // configure ADC1
    ADC1->CR1 = ADC_CR1_SCAN;  // Enable scan mode
    ADC1->CR2 = ADC_CR2_DMA |  // Enable DMA
                ADC_CR2_DDS |  // DMA in continuous mode
                ADC_CR2_CONT | // Continuous conversion
                ADC_CR2_ADON;  // Turn on ADC

    // set sample time for channels 10–13 to 480 cycles
    const uint8_t sample_time_val = 7U;
    ADC1->SMPR1 |= (sample_time_val << ADC_SMPR1_SMP10_Pos) | // CH10
                   (sample_time_val << ADC_SMPR1_SMP11_Pos) | // CH11
                   (sample_time_val << ADC_SMPR1_SMP12_Pos) | // CH12
                   (sample_time_val << ADC_SMPR1_SMP13_Pos);  // CH13

    // regular sequence config (SQR3: channels, SQR1: length)
    ADC1->SQR3 = (10U << ADC_SQR3_SQ1_Pos) | //  CH10 (PC0)
                 (11U << ADC_SQR3_SQ2_Pos) | //  CH11 (PC1)
                 (12U << ADC_SQR3_SQ3_Pos) | //  CH12 (PC2)
                 (13U << ADC_SQR3_SQ4_Pos);  //  CH13 (PC3)

    // 4 conversions (L = 3 -> 4 conversions)
    ADC1->SQR1 = (3U << ADC_SQR1_L_Pos);

    // start ADC conversion
    ADC1->CR2 |= ADC_CR2_SWSTART;
}