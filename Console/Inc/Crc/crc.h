#ifndef __CRC_CALCULATION_H
#define __CRC_CALCULATION_H

#include <stdint.h>

uint16_t crc16_calculate(const uint8_t *data, uint32_t length);

uint32_t crc32_calculate(const uint8_t *data, uint32_t length);

/*
 * Streaming CRC-32 (same zlib/IEEE variant as crc32_calculate). For data that
 * doesn't fit in RAM (e.g. a file downloaded chunk-by-chunk):
 *   uint32_t crc = CRC32_INIT;
 *   crc = crc32_update(crc, chunk, n);   // per chunk
 *   uint32_t result = crc32_final(crc);  // once at the end
 */
#define CRC32_INIT 0xFFFFFFFFU
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);
uint32_t crc32_final(uint32_t crc);

#endif