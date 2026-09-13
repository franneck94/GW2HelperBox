#ifndef SHARED_H
#define SHARED_H

#include <windows.h>

#include <filesystem>
#include <vector>

#include "mumble/Mumble.h"
#include "nexus/Nexus.h"
#include "rtapi/RTAPI.hpp"

namespace Globals
{
    extern AddonAPI *APIDefs;
    extern NexusLinkData *NexusLink;
    extern RTAPI::RealTimeData *RTAPIData;

    extern const char *KB_TOGGLE_GW2HB;
    extern const char* ADDON_NAME;

    extern std::filesystem::path AddonPath;
    extern std::filesystem::path SettingsPath;

    extern std::vector<uint32_t> CurrentlyPressedKeys;
    extern PROCESS_INFORMATION ForgeProcessInfo;
    extern bool ForgeProcessActive;
    extern PROCESS_INFORMATION AutoClickerProcessInfo;
    extern bool AutoClickerProcessActive;
    extern PROCESS_INFORMATION GW2TPServerProcessInfo;
    extern bool GW2TPServerProcessActive;

    bool IsInCombat();
}

#endif
