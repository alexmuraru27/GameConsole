#include "sound.h"
#include "assets.h"
#include "game_console_api.h"

#define SOUND_BUFFER_SIZE 128U  // bytes; supports up to 64 notes
DECLARE_API_HEADER_PTR(s_api_ptr);

static uint16_t s_notes_data[SOUND_BUFFER_SIZE / sizeof(uint16_t)];

void playSound(const uint16_t sound_asset_id)
{
    AssetMetaData asset_metadata;
    if (!s_api_ptr->api.assetLoaderGetAssetMetadata(sound_asset_id, &asset_metadata) &&
        asset_metadata.asset_type == ASSET_TYPE_AUDIO_DATA)
    {
        if (!s_api_ptr->api.assetLoaderGetAssetData(sound_asset_id, (uint8_t *)s_notes_data, SOUND_BUFFER_SIZE))
        {
            s_api_ptr->api.buzzerStopAll();
            s_api_ptr->api.buzzerPlay(0U, false, s_notes_data, asset_metadata.asset_size / 2U);
        }
    }
}
