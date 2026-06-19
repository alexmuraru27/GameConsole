#include "crc.h"

#define CRC16_POLYNOMIAL 0x1021U     // CRC-16 CCITT normal poly
#define CRC32_POLYNOMIAL 0xEDB88320U // CRC-32 IEEE reflected poly (zlib)

uint16_t crc16_calculate(const uint8_t *const data, const uint32_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8U;
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if (crc & 0x8000U)
            {
                crc = (crc << 1U) ^ CRC16_POLYNOMIAL;
            }
            else
            {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *const data, const uint32_t length)
{
    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= (uint32_t)data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1U) ^ CRC32_POLYNOMIAL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

uint32_t crc32_final(const uint32_t crc)
{
    return crc ^ 0xFFFFFFFFU;
}

uint32_t crc32_calculate(const uint8_t *const data, const uint32_t length)
{
    return crc32_final(crc32_update(CRC32_INIT, data, length));
}
