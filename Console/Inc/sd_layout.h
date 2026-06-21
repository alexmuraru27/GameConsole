#ifndef __SD_LAYOUT_H
#define __SD_LAYOUT_H

/*
 * SD card directory layout. Content is organized by purpose instead of dumped in
 * the volume root. The directories are created at mount time (loader.c) so they
 * always exist; every module agrees on the names through this header.
 *
 *   Games/      game .bin + its paired .pak
 *   Settings/   server.txt and other config text
 *   Firmware/   ESP01.bin (and future console OS images)
 *   Manifests/  the fetched remote manifest + the local downloaded-diff record
 */
#define SD_DIR_GAMES "Games"
#define SD_DIR_SETTINGS "Settings"
#define SD_DIR_FIRMWARE "Firmware"
#define SD_DIR_MANIFESTS "Manifests"

#endif /* __SD_LAYOUT_H */
