#include "Shared.h"

namespace Globals
{
    AddonAPI *APIDefs = nullptr;
    NexusLinkData *NexusLink = nullptr;
    RTAPI::RealTimeData *RTAPIData = nullptr;

    const char *KB_TOGGLE_GW2HB = "KB_TOGGLE_GW2HB";
    const char *ADDON_NAME = "GW2HB++";

    std::filesystem::path AddonPath = {};
    std::filesystem::path SettingsPath = {};

    std::vector<uint32_t> CurrentlyPressedKeys;
    PROCESS_INFORMATION ForgeProcessInfo = {};
    bool ForgeProcessActive = false;
    PROCESS_INFORMATION AutoClickerProcessInfo = {};
    bool AutoClickerProcessActive = false;
    PROCESS_INFORMATION GW2TPServerProcessInfo = {};
    bool GW2TPServerProcessActive = false;

    bool IsInCombat()
    {
        if (RTAPIData == nullptr || RTAPIData->GameBuild == 0)
            return false;

        return (static_cast<uint32_t>(RTAPIData->CharacterState) & static_cast<uint32_t>(RTAPI::ECharacterState::IsInCombat)) != 0;
    }
}
