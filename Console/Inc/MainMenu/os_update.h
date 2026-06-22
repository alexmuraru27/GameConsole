#ifndef __OS_UPDATE_H
#define __OS_UPDATE_H

/*
 * "Upgrade OS" flow. A blocking, full-screen modal (like "Upgrade WiFi module"):
 * finds Firmware/Console.bin on the SD card, streams it into the staging region of
 * internal flash with an on-screen progress bar, verifies it by readback, then
 * commits it and reboots into the bootloader, which applies and re-verifies it
 * before running. Invoked as a settings ACTION leaf under Firmware.
 */
void osUpdateRun(void);

#endif /* __OS_UPDATE_H */
