
#ifndef __ASSET_API_H
#define __ASSET_API_H
#include <stdint.h>
#include "stdbool.h"

// TODO use packer structs
typedef struct
{
    char magic[4];
    uint32_t version;
    uint32_t asset_count;
} __attribute__((packed, aligned(1))) AssetHeader;

typedef struct
{
    uint32_t id;
    uint32_t asset_type;
    uint32_t asset_size;
    uint32_t memory_size;
} __attribute__((packed, aligned(1))) AssetMetaData;

#endif /* __ASSET_API_H */