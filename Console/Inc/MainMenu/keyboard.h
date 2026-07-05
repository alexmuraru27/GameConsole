#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * On-screen keyboard, shown as a blocking full-screen modal — the single entry
 * used both by the console's own settings screens (WiFi password, server address,
 * player name) and by games through the osTextInput syscall.
 *   - RIGHT pad  : move around the key grid
 *   - LEFT pad   : move the text caret (left/right step, up/down = home/end)
 *   - Special 1  : type the highlighted key (inserts at the caret)
 *   - Special 2  : cancel
 * SHIFT/SPACE/DEL(backspace at caret)/DONE live on the bottom action row.
 *
 * Fills `out` (NUL-terminated, capacity `out_size`); the caller may pre-fill it to
 * offer a default / edit an existing value. The typed text is always shown in
 * clear. Returns true if confirmed via DONE, false if cancelled.
 *
 * The modal paints the standard menu backdrop while open and restores the caller's
 * renderer background on close, so it can be popped over a running game (which sets
 * its background once at init) without disturbing it. The caller redraws its own
 * screen on its next render().
 */
bool keyboardModal(const char *title, char *out, uint16_t out_size);

#endif /* __KEYBOARD_H */
