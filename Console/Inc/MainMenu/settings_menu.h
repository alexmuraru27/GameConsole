#ifndef __SETTINGS_MENU_H
#define __SETTINGS_MENU_H

#include "MainMenu/menu_common.h"

/* The settings screen: a navigable tree of typed settings. Today it holds a
 * single buzzer-sound toggle; categories and more leaves slot in as data. */
void settingsMenuEnter(void);            /* reset to the tree root, refresh surface */
MenuTransition settingsMenuUpdate(void); /* walk the tree; GOTO_ROOT at top + back */
void settingsMenuRender(void);

#endif /* __SETTINGS_MENU_H */
