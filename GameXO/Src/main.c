#include "game_console_api.h"
#include "assets.h"

DECLARE_API_HEADER_PTR(api_hdr_ptr);

int main(void)
{
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == API_VERSION)
    {
        api_hdr_ptr->api.debugString("Hello from GameXO :D\r\n");
    }
}

DECLARE_GAME_BINARY_HEADER(main);