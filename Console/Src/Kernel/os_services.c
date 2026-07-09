#include "os_services.h"

/* Provider registered by the app layer (MainMenu's keyboard). NULL until wired in;
 * a game that traps osTextInput before then simply gets a "cancelled" result. */
static OsTextInputFn s_text_input = 0;

void osServicesSetTextInput(OsTextInputFn fn)
{
    s_text_input = fn;
}

bool osServicesTextInput(const char *title, char *out, uint16_t out_size)
{
    if (s_text_input == 0)
    {
        return false;
    }
    return s_text_input(title, out, out_size);
}
