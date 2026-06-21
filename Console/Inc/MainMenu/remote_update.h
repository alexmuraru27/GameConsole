#ifndef __REMOTE_UPDATE_H
#define __REMOTE_UPDATE_H

/*
 * Remote flows over WiFi (blocking modals), each ensuring a WiFi connection
 * first (using saved credentials):
 *   - remoteGamesRun(): Poll Updates (main menu) — fetch the manifest, list every
 *     item with a NEW/UPD/UpToDate diff vs the last download, and pull the
 *     selected one (CRC-verified). The ESP firmware is just another row here.
 */
void remoteGamesRun(void);

/* Settings -> WiFi -> Server address: edit Settings/server.txt via the keyboard. */
void remoteServerAddrRun(void);

#endif /* __REMOTE_UPDATE_H */
