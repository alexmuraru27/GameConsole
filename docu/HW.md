- [Hardware needed](#hardware-needed)
- [Console board schematic](#console-board-schematic)
- [Pinning](#pinning)
  - [USART2 (Debug Interface - Baud 921600)](#usart2-debug-interface---baud-921600)
  - [DAC (Audio)](#dac-audio)
  - [SPI1 (Display ILI9341)](#spi1-display-ili9341)
  - [ADC1 (Analog Joysticks)](#adc1-analog-joysticks)
  - [GPIO (Button Joysticks)](#gpio-button-joysticks)
  - [SD-CARD (Builtin)](#sd-card-builtin)
  - [I2C1 EEPROM Console Settings Storage](#i2c1-eeprom-console-settings-storage)
  - [ESP01 - USART3 (Network Communication - Baud 921600)](#esp01---usart3-network-communication---baud-921600)

## Hardware needed
1. STM32F407VET6 development board
2. ILI9341 240x320 2.4" SPI display
3. Passive Buzzer
4. 10 push buttons
5. 2 analog joysticks with 2 axes
6. USB to TTL
7. ESP-01
8. AT24C256 I2C EEPROM module
9. 1 microSD card reader and microSD card FAT32 formatted

## Console board schematic
Hardware design resources  `./docu/HW/`

![PCB Design](docu/HW/PCB_DESIGN.png)

## Pinning
### USART2 (Debug Interface - Baud 921600) 
1. PA2 (TX - AF7)
2. PA3 (RX - AF7)

### DAC (Audio) 
1. PA4 (DAC1_OUT - Buzzer)
   
### SPI1 (Display ILI9341) 
1. PA5 (SCK - AF5)  - Yellow
2. PA6 (MISO - AF5) - Red
3. PA7 (MOSI - AF5) - Green
4. PA9 (DC - Normal GPIO AF) - Blue
5. PC7 (RST - Normal GPIO AF)- Purple
6. PB6 (CS - Normal GPIO AF) - Gray

### ADC1 (Analog Joysticks)
1. PC0 (ADC123_IN10 - Left Joystick X axis)
2. PC1 (ADC123_IN11 - Left Joystick Y axis)
3. PC2 (ADC123_IN12 - Right Joystick X axis)
4. PC3 (ADC123_IN13 - Right Joystick Y axis)

### GPIO (Button Joysticks)
1. PE7 (Right D-Pad UP)
2. PE8 (Right D-Pad RIGHT)
3. PE9 (Right D-Pad DOWN)
4. PE10 (Right D-Pad LEFT)
5. PE11 (Left D-Pad UP)
6. PE12 (Left D-Pad RIGHT)
7. PE13 (Left D-Pad DOWN)
8. PE14 (Left D-Pad LEFT)
9. PB11 (Special Button 1)
10. PB12 (Special Button 2)

### SD-CARD (Builtin)
1. PC10 (DAT2 - AF12 PU)
2. PC11 (CD/DAT3 - AF12 PU)
3. PD2 (CMD - AF12 PU)
4. PC12 (CLK - AF12)
5. PC8 (DAT0 - AF12 PU)
6. PC9 (DAT1 - AF12 PU)
7. PD3 (SD card detected - PU)

### I2C1 EEPROM Console Settings Storage
1. PB8 (I2C1_SCL - AF4 PU)
2. PB9 (I2C2_SDA - AF4 PU)

### ESP01 - USART3 (Network Communication - Baud 921600) 
1. PD8 (TX - AF7) - USART3
2. PD9 (RX - AF7) - USART3
3. PD10 (EN\ ESP8266 - normal GPIO)
4. PB10 (RST - normal GPIO)
5. PC6 (IO 0 - normal GPIO)
6. PC13 (IO 2 - normal GPIO)