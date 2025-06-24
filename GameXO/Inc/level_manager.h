
#ifndef __LEVEL_MANAGER_H
#define __LEVEL_MANAGER_H
#include <stdint.h>
#include "stdbool.h"
void levelManagerInit();

bool levelManagerChooseSymbol(bool is_level_transition);
// Returns true if game is in progress
bool levelManagerPlay(bool is_level_transition);
bool levelManagerEnd(bool is_level_transition);

#endif /* __LEVEL_MANAGER_H */