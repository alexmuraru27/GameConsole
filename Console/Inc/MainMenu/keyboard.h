#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Blocking on-screen keyboard (e.g. for the WiFi password / server address).
 *   - RIGHT pad  : move around the key grid
 *   - LEFT pad   : move the text caret (left/right step, up/down = home/end)
 *   - Special 1  : type the highlighted key (inserts at the caret)
 *   - Special 2  : cancel
 * SHIFT/SPACE/DEL(backspace at caret)/DONE live on the bottom action row.
 *
 * Fills `out` (NUL-terminated, capacity `out_size`); the caller may pre-fill it
 * to edit an existing value. Returns true if confirmed via DONE, false if
 * cancelled. When `mask` is true the typed text is shown as asterisks.
 */
bool keyboardEnter(const char *title, char *out, uint16_t out_size, bool mask);

#endif /* __KEYBOARD_H */
