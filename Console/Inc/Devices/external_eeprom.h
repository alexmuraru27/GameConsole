#ifndef __EXTERNAL_EEPROM_H
#define __EXTERNAL_EEPROM_H
#include "stdint.h"

#define EXTERNAL_EEPROM_AT24C256_ADDRESS 0x50U

void externalEepromInit(uint32_t device_address);
uint8_t externalEepromWrite(uint16_t mem_addr, uint8_t *data, uint16_t length);
uint8_t externalEepromRead(uint16_t mem_addr, uint8_t *data, uint16_t length);
#endif /* __EXTERNAL_EEPROM_H */