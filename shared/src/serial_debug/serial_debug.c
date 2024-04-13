#include "shared/include/serial_debug/serial_debug.h"
#include <string.h>

// Max APB1 42MHz
// UART 4 TX PC 10
// UART 4 RX PC 11
// From catalog table, for 42MHz on APB1 speed 115.2 KBps, BRR value must be 22.8125 with an error of 0.11
// BAUDRATE = 115200
#define BRR_VALUE ((22U << USART_BRR_DIV_Mantissa_Pos) | 13U)
static volatile serial_debug_data_struct serial_debug_data;

static void serial_gpio_init()
{
    // PC10 - Alternate function
    GPIOC->MODER &= ~GPIO_MODER_MODER10_Msk;
    GPIOC->MODER |= GPIO_MODER_MODER10_1;

    // PC10 Alternate function 8 = UART4_TX
    GPIOC->AFR[1] &= ~GPIO_AFRH_AFRH2;
    GPIOC->AFR[1] |= GPIO_AFRH_AFRH2_3;
}

void serial_init(void)
{
    // Init Gpio
    serial_gpio_init();

    // Uart setup
    UART4->BRR = BRR_VALUE;
    UART4->CR1 |= USART_CR1_UE;
    UART4->CR1 |= USART_CR1_TE;
    __NVIC_EnableIRQ(UART4_IRQn);
}

void UART4_IRQHandler(void)
{
    if ((UART4->SR & USART_SR_TXE) == USART_SR_TXE)
    {
        if ((serial_debug_data.uart_tx_tail + 1) % SERIAL_BUFFER_SIZE != serial_debug_data.uart_tx_head)
        {
            UART4->DR = serial_debug_data.uart_write_buffer[serial_debug_data.uart_tx_tail];
            serial_debug_data.uart_tx_tail = (serial_debug_data.uart_tx_tail + 1U) % SERIAL_BUFFER_SIZE;
        }
        else
        {
            UART4->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

void uart_write_string(char *str)
{
    if (strlen(str) < SERIAL_BUFFER_SIZE)
    {
        // Using a temporary counter to avoid corrupting the tx_head with incomplete data due to interrupt reading
        for (uint8_t i = 0U; i <= strlen(str); ++i)
        {
            serial_debug_data.uart_write_buffer[serial_debug_data.uart_tx_head] = str[i];
            serial_debug_data.uart_tx_head = (serial_debug_data.uart_tx_head + 1U) % SERIAL_BUFFER_SIZE;
        }

        UART4->CR1 |= USART_CR1_TXEIE;
    }
}

void uart_write_int(uint32_t num)
{
    char buf[20U];
    uint8_t nr_digits = 0;
    do
    {
        buf[nr_digits] = '0' + (num % 10);
        num /= 10;
        nr_digits++;
    } while (num != 0);

    for (uint8_t i = 0; i < nr_digits; i++)
    {
        serial_debug_data.uart_write_buffer[serial_debug_data.uart_tx_head] = buf[nr_digits - 1 - i];
        serial_debug_data.uart_tx_head = (serial_debug_data.uart_tx_head + 1U) % SERIAL_BUFFER_SIZE;
    }
    UART4->CR1 |= USART_CR1_TXEIE;
}