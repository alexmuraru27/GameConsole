# GameConsole

## Board type
https://stm32-base.org/boards/STM32F407VET6-STM32-F4VE-V2.0.html

## Name Conventions 

| Element             | Suggested Case        | Example                         |
|---------------------|-----------------------|---------------------------------|
| **Macros/Defines**  | UPPER_SNAKE_CASE      | `#define MAX_BUFFER_SIZE 256`   |
| **Constants**       | UPPER_SNAKE_CASE      | `const int DEFAULT_TIMEOUT`     |
| **Global variables**| g_snake_case          | `g_system_initialized`          |
| **Static globals**  | s_snake_case          | `s_buffer_index`                |
| **Local variables** | snake_case            | `temp_value`                    |
| **Functions**       | snake_case or camelCase | `init_peripherals()` or `initPeripherals()` |
| **Struct types**    | PascalCase            | `typedef struct SensorData`     |
| **Enum types**      | PascalCase            | `typedef enum PowerState`       |
| **Struct members**  | snake_case            | `uint16_t adc_value;`           |
| **Typedefs**        | PascalCase            | `typedef uint8_t Byte;`         |

## Defines:
- #define USART_BUFFER_SIZE ((uint32_t)512U)
- #define SYS_TICK_SECOND_DIV ((uint32_t)1000U)
- #define HSE_CLOCK_VALUE ((uint32_t)8000000)
- #define HSI_CLOCK_VALUE ((uint32_t)16000000)

## Pinning:
### USART2 (Debug Interface - Baud 921600) 
1. PA2 (TX - AF7)
2. PA3 (RX - AF7)

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
1. PC10 (DAT2)
2. PC11 (CD/DAT3)
3. PD2 (CMD)
4. PC12 (CLK)
5. PC8 (DAT0)
6. PC9 (DAT1)


## Default System Color Palette
![system_palette](docu/system_palette.png)


## tile_creator.py
### Deps: pygame

### User inputs:
1. Left click draw
2. Right click clear
3. "S" to export the tile into generated_tile.h

### Usage
Click on the 4 big squares to select the palette with which to draw (1st one is always the transparent one)
After you selected a palette you can also change its color from the top grid containing the system palette
Output directory: generated_tiles/

![alt text](docu/tile_creator_docu.png)

E.g output

```c++
DEFINE_TILE_16(
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

```