#ifndef __REMOTE_UPDATE_H
#define __REMOTE_UPDATE_H

/*
 * Remote download flows over WiFi (blocking modals). Both fetch the update
 * server manifest, then download + CRC-verify files to the SD card with an
 * on-screen progress bar:
 *   - remoteGamesRun()        : Poll Remote Games (main menu) — pick a game to pull.
 *   - remoteWifiFirmwareRun() : Settings -> Download WiFi firmware (the ESP image).
 * Both ensure a WiFi connection first (using saved credentials).
 */
void remoteGamesRun(void);
void remoteWifiFirmwareRun(void);

/* Settings -> Server address: edit 0:/server.txt via the on-screen keyboard. */
void remoteServerAddrRun(void);

#endif /* __REMOTE_UPDATE_H */
