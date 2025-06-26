#ifndef __EXTERNAL_EEPROM_H
#define __EXTERNAL_EEPROM_H
#include "stdint.h"

#define EXTERNAL_EEPROM_AT24C256_ADDRESS 0x50U

void externalEepromInit(uint32_t device_address);
#endif /* __EXTERNAL_EEPROM_H */