#ifndef SETTINGS_H
#define SETTINGS_H

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mumble/Mumble.h"
#include "nexus/Nexus.h"
#include "rtapi/RTAPI.hpp"

#include "Types.h"

extern const char *SHOW_WINDOW;
extern const char *FORGE_VERSION;
extern const char *BACKEND_VERSION;
extern const char *CLICKER_VERSION;

namespace Settings
{
    extern std::mutex Mutex;
    extern json Settings;

    void Load(std::filesystem::path SettingsPath);
    void Save(std::filesystem::path SettingsPath);

    void ToggleShowWindow(std::filesystem::path SettingsPath);

    extern bool ShowWindow;
    extern std::string ForgeVersion;
    extern std::string BackendVersion;
    extern std::string ClickerVersion;
    extern float EctoRate;
    extern std::string BackendVersion;
    extern std::string ClickerVersion;
    extern std::string APIKey;
    extern bool ScriptsInBackground;
    extern bool ShowOutdatedOrders;
    extern bool HideNotificationsInCombat;
    extern bool RaidBossesOnly;
    extern std::set<int> NotifiedOutdatedOrderIds;
    extern std::vector<CustomItem> CustomItems;
    extern std::vector<WatchlistPriceAlert> WatchlistPriceAlerts;
    extern std::set<std::string> FinishedCollections;
    extern std::vector<SecondaryAccount> SecondaryAPIKeys;
    extern std::map<std::string, CompletionCache> CompletionCaches;
    extern std::map<std::string, std::vector<CharacterInfo>> CharacterCaches;
}

#endif
