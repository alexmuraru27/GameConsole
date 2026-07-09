#ifndef __NETWORK_SERVER_CONFIG_H
#define __NETWORK_SERVER_CONFIG_H

/*
 * Update-server address configuration — owns 0:/server.txt (read/write, and
 * auto-creating an example template) and composes request URLs from it. Split out
 * of downloader.c because the server address has nothing to do with the actual
 * transfer; the public downloaderGetServerAddr/SetServerAddr (declared in
 * downloader.h) are implemented here, and the transfer code reaches the composed
 * URL through downloaderMakeUrl().
 */

/* Compose the full request URL for `path` ("http://" + configured base + "/" +
 * path) into an internal static buffer and return it (valid until the next call). */
const char *downloaderMakeUrl(const char *path);

#endif /* __NETWORK_SERVER_CONFIG_H */
