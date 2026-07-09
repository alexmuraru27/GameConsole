#ifndef __EXTERNAL_EEPROM_H
#define __EXTERNAL_EEPROM_H
#include <stdint.h>

#define EXTERNAL_EEPROM_AT24C512_PAGE_SIZE 128U
#define EXTERNAL_EEPROM_AT24C512_ADDRESS 0x50U
#define EXTERNAL_EEPROM_AT24C512_MAX_MEMORY_ADDR 65535U

void externalEepromInit(uint32_t device_address);
uint8_t externalEepromWrite(uint16_t mem_addr, const uint8_t *data, uint16_t length);
uint8_t externalEepromRead(uint16_t mem_addr, uint8_t *data, uint16_t length);
uint8_t externalEepromClearRange(uint16_t mem_addr, uint32_t length);
uint8_t externalEepromClear();
#endif /* __EXTERNAL_EEPROM_H */