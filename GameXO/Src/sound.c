#include "sound.h"
#include "stdbool.h"
#include "assets.h"
#include "game_console_api.h"

#define SOUND_BUFFER_SIZE 128U
DECLARE_API_HEADER_PTR(s_api_ptr);

static uint16_t s_sound_buffer[SOUND_BUFFER_SIZE];
static uint16_t s_sound_duration_buffer[SOUND_BUFFER_SIZE];

void playSound(const uint16_t sound_asset_id, const uint16_t sound_duration_asset_id)
{
    AssetMetaData asset_metadata;
    if (!s_api_ptr->api.assetLoaderGetAssetMetadata(sound_asset_id, &asset_metadata) && asset_metadata.asset_type == ASSET_TYPE_AUDIO_DATA)
    {
        if ((!s_api_ptr->api.assetLoaderGetAssetData(sound_asset_id, (uint8_t *)s_sound_buffer, SOUND_BUFFER_SIZE)) && (!s_api_ptr->api.assetLoaderGetAssetData(sound_duration_asset_id, (uint8_t *)s_sound_duration_buffer, SOUND_BUFFER_SIZE)))
        {
            s_api_ptr->api.buzzerStopAll();
            s_api_ptr->api.buzzerPlay(0U, false, s_sound_buffer, s_sound_duration_buffer, asset_metadata.asset_size);
        }
    }
}