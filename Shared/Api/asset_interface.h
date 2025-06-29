
#ifndef __ASSET_API_H
#define __ASSET_API_H
#include <stdint.h>
#include "stdbool.h"

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

typedef struct
{
    AssetMetaData metadata;
    uint8_t data[];
} __attribute__((packed, aligned(1))) AssetData8;

typedef struct
{
    AssetMetaData metadata;
    uint16_t data[];
} __attribute__((packed, aligned(1))) AssetData16;

typedef struct
{
    AssetMetaData metadata;
    uint32_t data[];
} __attribute__((packed, aligned(1))) AssetData32;

//  aligned(1) -> ensure continuous structure asset space
#define _ASSET_SECTION_HEADER __attribute__((section(".assets.header"), aligned(1)))
#define _ASSET_SECTION_DATA __attribute__((section(".assets.data"), aligned(1)))

#define EXPAND_DATA(...) __VA_ARGS__

#define _DEFINE_ASSET(name_param, asset_struct_param, data_type_param, asset_id_param, asset_type_param, asset_data_param)                                                                              \
    _ASSET_SECTION_DATA const asset_struct_param name_param = {                                                                                                                                         \
        .metadata = {asset_id_param, asset_type_param, sizeof((data_type_param[]){EXPAND_DATA asset_data_param}) / sizeof(data_type_param), sizeof((data_type_param[]){EXPAND_DATA asset_data_param})}, \
        .data = {EXPAND_DATA asset_data_param}}

#define DEFINE_ASSET_HEADER(char_4_magic, version_nr, asset_count_nr) \
    _ASSET_SECTION_HEADER const AssetHeader asset_header = {          \
        .magic = char_4_magic,                                        \
        .version = version_nr,                                        \
        .asset_count = asset_count_nr}

#define DEFINE_ASSET_8(name, asset_id, asset_type, asset_data) \
    _DEFINE_ASSET(name, AssetData8, uint8_t, asset_id, asset_type, asset_data)

#define DEFINE_ASSET_16(name, asset_id, asset_type, asset_data) \
    _DEFINE_ASSET(name, AssetData16, uint16_t, asset_id, asset_type, asset_data)

#define DEFINE_ASSET_32(name, asset_id, asset_type, asset_data) \
    _DEFINE_ASSET(name, AssetData32, uint32_t, asset_id, asset_type, asset_data)

#endif /* __ASSET_API_H */