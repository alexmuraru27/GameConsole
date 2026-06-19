#ifndef __WIFI_MENU_H
#define __WIFI_MENU_H

/*
 * WiFi settings flow (Settings -> WiFi). Blocking modal: scans for access
 * points, lets you pick one and enter its password on the on-screen keyboard,
 * connects via the ESP, and persists the credentials so future sessions
 * reconnect without retyping. Returns on Special Button 2.
 */
void wifiMenuRun(void);

#endif /* __WIFI_MENU_H */
