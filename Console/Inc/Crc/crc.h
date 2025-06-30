#ifndef __CRC_CALCULATION_H
#define __CRC_CALCULATION_H

#include "stdint.h"

uint16_t crc16_calculate(const uint8_t *data, uint32_t length);

#endif