#include "shared/include/SPI.h"

void SPI2_Init(void)
{
    // PB09 AF5 - NSS
    // PB10 AF5 - CLK SPI2
    // PB15 AF5 - SDI

    GPIOB->MODER &= ~(GPIO_MODER_MODER9);
    GPIOB->MODER &= ~(GPIO_MODER_MODER10);
    GPIOB->MODER &= ~(GPIO_MODER_MODER15);

    GPIOB->MODER |= (GPIO_MODER_MODER9_1);
    GPIOB->MODER |= (GPIO_MODER_MODER10_1);
    GPIOB->MODER |= (GPIO_MODER_MODER15_1);

    GPIOB->AFR[1] &= ~GPIO_AFRL_AFRL1;
    GPIOB->AFR[1] &= ~GPIO_AFRL_AFRL2;
    GPIOB->AFR[1] &= ~GPIO_AFRL_AFRL7;

    GPIOB->AFR[1] |= GPIO_AFRL_AFRL1_2;
    GPIOB->AFR[1] |= GPIO_AFRL_AFRL1_0;

    GPIOB->AFR[1] |= GPIO_AFRL_AFRL2_2;
    GPIOB->AFR[1] |= GPIO_AFRL_AFRL2_0;

    GPIOB->AFR[1] |= GPIO_AFRL_AFRL7_2;
    GPIOB->AFR[1] |= GPIO_AFRL_AFRL7_0;

    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED9;
    GPIOB->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED9_Pos);

    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED10;
    GPIOB->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED10_Pos);

    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED15;
    GPIOB->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED15_Pos);

    // Disable SPI2
    SPI2->CR1 &= ~SPI_CR1_SPE;

    // Set baud rate prescaler to fPCLK/32
    SPI2->CR1 |= SPI_CR1_BR_0;

    // Set data frame format to 8-bit
    SPI2->CR1 &= ~SPI_CR1_DFF;

    // CPOL CPHA
    // SPI2->CR1 |= (1 << 0) | (1 << 1);
    SPI2->CR1 |= SPI_CR1_CPHA | SPI_CR1_CPOL;

    // Set MSB first
    SPI2->CR1 &= ~SPI_CR1_LSBFIRST;

    // NSS software mode
    SPI2->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;
    SPI2->CR2 |= SPI_CR2_SSOE;

    SPI2->CR1 = SPI_CR1_MSTR;

    // Enable SPI2
    SPI2->CR1 |= SPI_CR1_SPE;
}

void SPI2_SendData(uint8_t *buff, size_t buff_size)
{
    for (uint8_t idx = 0U; idx < buff_size; ++idx)
    {
        // Wait until TX buffer is empty
        while (!(SPI2->SR & SPI_SR_TXE))
            ;

        // Send data
        SPI2->DR = buff[idx];

        // Wait until transmission is complete
        while (SPI2->SR & SPI_SR_BSY)
            ;
    }
}
