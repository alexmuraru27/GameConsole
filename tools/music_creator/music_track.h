// This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
#ifndef __MUSIC_TRACK_H
#define __MUSIC_TRACK_H
#include <stdint.h>

// "NOT1" as a little-endian uint32 ('N' 'O' 'T' '1' in byte order)
#define NOTE_ASSET_MAGIC 0x31544F4EU
#define NOTE_ASSET_VERSION 1U

typedef struct __attribute__((packed))
{
    uint16_t frequency_hz; // 0 = pause
    uint16_t duration_ms;
} NoteEntry;

typedef struct __attribute__((packed))
{
    uint32_t magic;      // "NOT1"
    uint32_t version;    // NOTE_ASSET_VERSION
    uint32_t noteCount;  // number of NoteEntry entries
    uint32_t dataSize;   // noteCount * sizeof(NoteEntry)
} MusicTrackHeader;

// Maps an entire .bin file (all little-endian): cast the loaded buffer to
// a MusicTrack pointer and validate header.magic and header.version.
typedef struct __attribute__((packed))
{
    MusicTrackHeader header;
    NoteEntry entries[]; // header.noteCount entries
} MusicTrack;

#endif /* __MUSIC_TRACK_H */
