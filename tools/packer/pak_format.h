#ifndef PAK_FORMAT_H
#define PAK_FORMAT_H

#include <stdint.h>

/*
 * .pak container format produced by packer.py.
 *
 * On-disk layout (all fields little-endian):
 *
 *     [PakHeader fixed fields]   magic .. pakCrc        (24 bytes)
 *     [PakEntry 0 .. N-1]        assetCount * 16 bytes
 *     [asset 0 .. N-1 blobs]     raw bytes, concatenated
 *
 * Every field is a uint32_t, so the structs are naturally contiguous (no
 * padding) and map directly onto the file bytes. PakHeader carries a flexible
 * array member, so sizeof(PakHeader) is the 24-byte fixed portion only.
 */

#define PAK_MAGIC 0x314B4150u /* "PAK1" little-endian */
#define PAK_VERSION 1u

typedef struct
{
    uint32_t id;     /* asset id */
    uint32_t offset; /* blob offset, relative to PakHeader.dataOffset */
    uint32_t size;   /* stored blob size in bytes */
    uint32_t crc32;  /* CRC32 of this blob */
} PakEntry;

typedef struct
{
    uint32_t magic;      /* PAK_MAGIC */
    uint32_t version;    /* PAK_VERSION */
    uint32_t assetCount; /* number of PakEntry records */
    uint32_t dataOffset; /* byte offset of the first blob (start of raw data) */
    uint32_t pakSize;    /* total file size in bytes */
    uint32_t pakCrc;     /* CRC32 of the whole file, excluding these 4 bytes */
    PakEntry entries[];  /* assetCount entries, followed by the raw blobs */
} PakHeader;

#endif /* PAK_FORMAT_H */
