#include "SPI.h"
#include "stm32f407xx.h"

void SPIInit(void)
{
    SPI1->CR1 = 0;
    SPI1->CR1 |= SPI_CR1_MSTR;              // master mode
    SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI; // software NSS management
    SPI1->CR1 |= 1U << SPI_CR1_BR_Pos;      // baud rate = fPCLK / 4
    SPI1->CR1 |= SPI_CR1_SPE;               // enable SPI
}

void spiWrite(const uint8_t data)
{
    // wait until TX buffer empty
    while (!(SPI1->SR & SPI_SR_TXE))
        ;
    SPI1->DR = data;
}

uint8_t spiRead()
{
    while (!(SPI1->SR & SPI_SR_RXNE))
        ;
    return SPI1->DR;
}