#include "SPI.h"
#include "stm32f407xx.h"

void SPIInit(void)
{
    SPI1->CR1 = 0;
    SPI1->CR1 |= SPI_CR1_MSTR;              // Master mode
    SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI; // Software NSS management
    SPI1->CR1 |= 7 << SPI_CR1_BR_Pos;       // Baud rate = fPCLK / 8
    SPI1->CR1 |= SPI_CR1_SPE;               // Enable SPI
}

void spiWrite(const uint8_t data)
{
    while (!(SPI1->SR & SPI_SR_TXE))
        ;            // Wait until TX buffer empty
    SPI1->DR = data; // Send byte
}

uint8_t spiRead()
{
    while (!(SPI1->SR & SPI_SR_RXNE))
        ;
    return SPI1->DR;
}