#include "game_console_api.h"
#include "assets.h"

extern ConsoleAPIHeader __game_console_api_start; // linker
ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;

// testing purposes
static volatile uint8_t s_data_array[5U] = {1U, 2U, 3U, 4U, 5U};
static volatile uint8_t s_bss[7U];

int main(void)
{
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == API_VERSION)
    {
        api_hdr_ptr->api.debugString("Hello from GameXO :D\r\n");

        uint32_t res = 0U;
        AssetMetaData asset_1_metadata;
        res = api_hdr_ptr->api.assetLoaderGetAssetMetadata(ASSET_ID_SEQ_DATA, &asset_1_metadata);
        if (!res)
        {
            uint8_t asset_1_data[asset_1_metadata.memory_size];
            res = api_hdr_ptr->api.assetLoaderGetAssetData(ASSET_ID_SEQ_DATA, asset_1_data);
            if (!res)
            {
                for (uint8_t i = 0U; i < asset_1_metadata.memory_size; i++)
                {
                    api_hdr_ptr->api.debugInt(asset_1_data[i]);
                    api_hdr_ptr->api.debugString(" ");
                }
            }
            else
            {
                api_hdr_ptr->api.debugString("\r\n asset1 data fail");
            }
        }
        else
        {
            api_hdr_ptr->api.debugString("\r\n asset1 size fail");
        }

        AssetMetaData asset_2_metadata;
        res = api_hdr_ptr->api.assetLoaderGetAssetMetadata(ASSET_ID_FONT_A, &asset_2_metadata);
        if (!res)
        {
            uint8_t asset_2_data[asset_2_metadata.memory_size];
            res = api_hdr_ptr->api.assetLoaderGetAssetData(ASSET_ID_FONT_A, asset_2_data);
            api_hdr_ptr->api.debugString("\r\n ");
            if (!res)
            {
                for (uint8_t i = 0U; i < asset_2_metadata.memory_size; i++)
                {
                    api_hdr_ptr->api.debugBinary(asset_2_data[i], 8);
                    api_hdr_ptr->api.debugChar(' ');
                    if (i % 2 == 1U)
                    {
                        api_hdr_ptr->api.debugString("\r\n ");
                    }
                }
            }
            else
            {
                api_hdr_ptr->api.debugString("\r\n asset2 data fail");
            }
        }
        else
        {
            api_hdr_ptr->api.debugString("\r\n asset2 size fail");
        }

        api_hdr_ptr->api.debugString("\r\nData:\r\n");
        api_hdr_ptr->api.debugInt(s_data_array[0]);
        api_hdr_ptr->api.debugInt(s_data_array[1]);
        api_hdr_ptr->api.debugInt(s_data_array[2]);
        api_hdr_ptr->api.debugInt(s_data_array[3]);
        api_hdr_ptr->api.debugInt(s_data_array[4]);
        api_hdr_ptr->api.debugString("\r\nBss:\r\n");
        api_hdr_ptr->api.debugInt(s_bss[0]);
        api_hdr_ptr->api.debugInt(s_bss[1]);
        api_hdr_ptr->api.debugInt(s_bss[2]);
        api_hdr_ptr->api.debugInt(s_bss[3]);
        api_hdr_ptr->api.debugInt(s_bss[4]);
        api_hdr_ptr->api.debugInt(s_bss[5]);
        api_hdr_ptr->api.debugInt(s_bss[6]);
        api_hdr_ptr->api.debugString("\r\n");
        api_hdr_ptr->api.debugString("In Loop\r\n");

        api_hdr_ptr->api.delay(500);
    }
}

extern uint32_t __game_header_start, __game_header_end;
extern uint32_t __game_text_start, __game_text_end;
extern uint32_t __game_ro_data_start, __game_ro_data_end;
extern uint32_t __game_data_init_start, __game_data_init_end;
extern uint32_t __game_data_no_init_start, __game_data_no_init_end;
extern uint32_t __game_code_assets_start, __game_code_assets_end;

__attribute__((section(".game_header")))
const GameHeader game_header = {
    .magic = 0x47414D45, // GAME
    .header_start = (uint32_t)&__game_header_start,
    .header_end = (uint32_t)&__game_header_end,
    .text_start = (uint32_t)&__game_text_start,
    .text_end = (uint32_t)&__game_text_end,
    .ro_data_start = (uint32_t)&__game_ro_data_start,
    .ro_data_end = (uint32_t)&__game_ro_data_end,
    .data_start = (uint32_t)&__game_data_init_start,
    .data_end = (uint32_t)&__game_data_init_end,
    .bss_start = (uint32_t)&__game_data_no_init_start,
    .bss_end = (uint32_t)&__game_data_no_init_end,
    .assets_start = (uint32_t)&__game_code_assets_start,
    .assets_end = (uint32_t)&__game_code_assets_end,
    .entry_point = (uint32_t)&main};
