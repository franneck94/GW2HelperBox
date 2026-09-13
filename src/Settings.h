#ifndef SETTINGS_H
#define SETTINGS_H

#include <mutex>
#include <set>
#include <vector>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

#include "mumble/Mumble.h"
#include "nexus/Nexus.h"
#include "rtapi/RTAPI.hpp"

extern const char *SHOW_WINDOW;
extern const char *FORGE_VERSION;
extern const char *BACKEND_VERSION;
extern const char *CLICKER_VERSION;

struct CustomItem
{
    int item_id = 0;
    std::string name; // user-defined display name shown in the table header instead of the raw item ID
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CustomItem, item_id, name)

struct SecondaryAccount
{
    std::string name;    // user-defined label shown in the Completions account picker
    std::string api_key; // only used for the Completions tab, never for trading
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SecondaryAccount, name, api_key)

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
    extern std::set<std::string> FinishedCollections;
    extern std::vector<SecondaryAccount> SecondaryAPIKeys;
}

#endif
