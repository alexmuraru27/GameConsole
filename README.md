# GameConsole
 
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
