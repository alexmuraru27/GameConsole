#include "game_console_api.h"

extern ConsoleAPIHeader __game_console_api_start; // linker
static void testApi()
{
    ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == API_VERSION)
    {
        api_hdr_ptr->api.debugString("Hello from game shared api :D\r\n");
    }
}

// testing purposes
static volatile uint8_t s_data_array[5U] = {1U, 2U, 3U, 4U, 5U};
static volatile uint8_t s_bss[7U];

#define ASSET_NUMBER_SEQ_DATA 15U
#define ASSET_NUMBER_SMALL_SEQ_DATA 16U

#define ASSET_TYPE_TEST_DATA_1 1U
#define ASSET_TYPE_TEST_DATA_2 2U

__attribute__((section(".assets.header")))
const AssetHeader asset_header = {
    .magic = {'G', 'A', 'M', 'E'},
    .version = 1U,
    .asset_count = 2U};

__attribute__((section(".assets.data"))) const AssetData test_data_1 = {
    .metadata = {.id = ASSET_NUMBER_SEQ_DATA,
                 .type = ASSET_TYPE_TEST_DATA_1,
                 .size = 16U},
    .data = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3}};

__attribute__((section(".assets.data"))) const AssetData test_data_2 = {
    .metadata = {
        .id = ASSET_NUMBER_SMALL_SEQ_DATA,
        .type = ASSET_TYPE_TEST_DATA_2,
        .size = 4U},
    .data = {0x20, 0x1c, 0x2c, 0xc}};

int main(void)
{
    ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;
    api_hdr_ptr->api.debugString("\r\nblahblah");

    uint32_t res = 0U;
    uint32_t asset_1_size = 0U;
    res = api_hdr_ptr->api.assetLoaderGetAssetSize(ASSET_NUMBER_SEQ_DATA, &asset_1_size);
    if (!res)
    {
        uint8_t asset_1_data[asset_1_size];
        res = api_hdr_ptr->api.assetLoaderGetAssetData(ASSET_NUMBER_SEQ_DATA, asset_1_data);
        if (!res)
        {
            for (uint8_t i = 0U; i < asset_1_size; i++)
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

    api_hdr_ptr->api.debugString("\r\n");
    uint32_t asset_2_size = 0U;
    res = api_hdr_ptr->api.assetLoaderGetAssetSize(ASSET_NUMBER_SMALL_SEQ_DATA, &asset_2_size);
    if (!res)
    {
        uint8_t asset_2_data[asset_2_size];
        res = api_hdr_ptr->api.assetLoaderGetAssetData(ASSET_NUMBER_SMALL_SEQ_DATA, asset_2_data);
        if (!res)
        {
            for (uint8_t i = 0U; i < asset_2_size; i++)
            {
                api_hdr_ptr->api.debugHex(asset_2_data[i]);
                api_hdr_ptr->api.debugString(" ");
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
    asm("nop");
    testApi();
    api_hdr_ptr->api.delay(500);
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
