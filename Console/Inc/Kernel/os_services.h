#ifndef __KERNEL_OS_SERVICES_H
#define __KERNEL_OS_SERVICES_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Services the trusted kernel needs from the app/UI layer, wired in at boot so the
 * kernel never has to include or link the menu UI (which would invert the layer
 * direction and drag the whole menu/renderer/font stack into the kernel's closure).
 * Today the only one is the on-screen keyboard behind the osTextInput syscall: the
 * app registers a provider once during bring-up and the kernel calls it downward.
 */

/* Blocking text-input modal: writes up to `out_size` bytes into `out`, returns true
 * if the user confirmed, false if they cancelled. Matches keyboardModal(). */
typedef bool (*OsTextInputFn)(const char *title, char *out, uint16_t out_size);

/* Register the text-input provider (called once at boot by the app layer). */
void osServicesSetTextInput(OsTextInputFn fn);

/* Kernel-side entry for the osTextInput syscall: delegates to the registered
 * provider, or returns false if none was registered. */
bool osServicesTextInput(const char *title, char *out, uint16_t out_size);

#endif /* __KERNEL_OS_SERVICES_H */
