#include "external_eeprom.h"

static uint32_t s_device_address = 0U;

void externalEepromInit(const uint32_t device_address)
{
    s_device_address = device_address;
}