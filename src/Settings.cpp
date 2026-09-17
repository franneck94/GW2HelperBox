#include <filesystem>
#include <fstream>

#include "Settings.h"
#include "Shared.h"

const char *SHOW_WINDOW = "ShowWindow";
const char *FORGE_VERSION = "ForgeVersion";
const char *BACKEND_VERSION = "BackendVersion";
const char *CLICKER_VERSION = "ClickerVersion";
const char *ECTO_RATE = "EctoRate";
const char *API_KEY = "APIKey";
const char *SCRIPTS_IN_BACKGROUND = "ScriptsInBackground";
const char *SHOW_OUTDATED_ORDERS = "ShowOutdatedOrders";
const char *HIDE_NOTIFICATIONS_IN_COMBAT = "HideNotificationsInCombat";
const char *RAID_BOSSES_ONLY = "RaidBossesOnly";
const char *NOTIFIED_OUTDATED_ORDER_IDS = "NotifiedOutdatedOrderIds";
const char *CUSTOM_ITEMS = "CustomItems";
const char *WATCHLIST_PRICE_ALERTS = "WatchlistPriceAlerts";
const char *FINISHED_COLLECTIONS = "FinishedCollections";
const char *SECONDARY_API_KEYS = "SecondaryAPIKeys";
const char *COMPLETION_CACHES = "CompletionCaches";
const char *CHARACTER_CACHES = "CharacterCaches";
const char *CUSTOM_ITEM_IDS_LEGACY = "CustomItemIds"; // pre-migration format: plain list of item IDs

namespace Settings
{
    std::mutex Mutex;
    json Settings = json::object();

    void Load(std::filesystem::path SettingsPath)
    {
        if (!std::filesystem::exists(SettingsPath))
        {
            return;
        }

        Settings::Mutex.lock();
        {
            try
            {
                std::ifstream file(SettingsPath);
                Settings = json::parse(file);
                file.close();
            }
            catch (json::parse_error &ex)
            {
                Globals::APIDefs->Log(ELogLevel_WARNING, Globals::ADDON_NAME, "Settings.json could not be parsed.");
                Globals::APIDefs->Log(ELogLevel_WARNING, Globals::ADDON_NAME, ex.what());
            }
        }
        Settings::Mutex.unlock();

        /* Widget */
        if (!Settings[SHOW_WINDOW].is_null())
            Settings[SHOW_WINDOW].get_to<bool>(ShowWindow);
        if (!Settings[FORGE_VERSION].is_null())
            Settings[FORGE_VERSION].get_to<std::string>(ForgeVersion);
        if (!Settings[BACKEND_VERSION].is_null())
            Settings[BACKEND_VERSION].get_to<std::string>(BackendVersion);
        if (!Settings[CLICKER_VERSION].is_null())
            Settings[CLICKER_VERSION].get_to<std::string>(ClickerVersion);
        if (!Settings[ECTO_RATE].is_null())
            Settings[ECTO_RATE].get_to<float>(EctoRate);
        if (!Settings[API_KEY].is_null())
            Settings[API_KEY].get_to<std::string>(APIKey);
        if (!Settings[SCRIPTS_IN_BACKGROUND].is_null())
            Settings[SCRIPTS_IN_BACKGROUND].get_to<bool>(ScriptsInBackground);
        if (!Settings[SHOW_OUTDATED_ORDERS].is_null())
            Settings[SHOW_OUTDATED_ORDERS].get_to<bool>(ShowOutdatedOrders);
        if (!Settings[HIDE_NOTIFICATIONS_IN_COMBAT].is_null())
            Settings[HIDE_NOTIFICATIONS_IN_COMBAT].get_to<bool>(HideNotificationsInCombat);
        if (!Settings[RAID_BOSSES_ONLY].is_null())
            Settings[RAID_BOSSES_ONLY].get_to<bool>(RaidBossesOnly);
        if (!Settings[NOTIFIED_OUTDATED_ORDER_IDS].is_null())
            Settings[NOTIFIED_OUTDATED_ORDER_IDS].get_to<std::set<int>>(NotifiedOutdatedOrderIds);
        if (!Settings[CUSTOM_ITEMS].is_null())
            Settings[CUSTOM_ITEMS].get_to<std::vector<CustomItem>>(CustomItems);
        else if (!Settings[CUSTOM_ITEM_IDS_LEGACY].is_null())
        {
            /* migrate the old plain-ID watchlist format */
            std::vector<int> legacy_ids;
            Settings[CUSTOM_ITEM_IDS_LEGACY].get_to<std::vector<int>>(legacy_ids);
            for (const auto item_id : legacy_ids)
                CustomItems.push_back(CustomItem{item_id, ""});
        }

        if (!Settings[WATCHLIST_PRICE_ALERTS].is_null())
            Settings[WATCHLIST_PRICE_ALERTS].get_to<std::vector<WatchlistPriceAlert>>(WatchlistPriceAlerts);

        if (!Settings[FINISHED_COLLECTIONS].is_null())
            Settings[FINISHED_COLLECTIONS].get_to<std::set<std::string>>(FinishedCollections);

        if (!Settings[SECONDARY_API_KEYS].is_null())
            Settings[SECONDARY_API_KEYS].get_to<std::vector<SecondaryAccount>>(SecondaryAPIKeys);

        if (!Settings[COMPLETION_CACHES].is_null())
            Settings[COMPLETION_CACHES].get_to<std::map<std::string, CompletionCache>>(CompletionCaches);

        if (!Settings[CHARACTER_CACHES].is_null())
            Settings[CHARACTER_CACHES].get_to<std::map<std::string, std::vector<CharacterInfo>>>(CharacterCaches);
    }

    void Save(std::filesystem::path SettingsPath)
    {
        Settings::Mutex.lock();
        {
            Settings[SHOW_WINDOW] = ShowWindow;
            Settings[FORGE_VERSION] = ForgeVersion;
            Settings[BACKEND_VERSION] = BackendVersion;
            Settings[CLICKER_VERSION] = ClickerVersion;
            Settings[ECTO_RATE] = EctoRate;
            Settings[API_KEY] = APIKey;
            Settings[SCRIPTS_IN_BACKGROUND] = ScriptsInBackground;
            Settings[SHOW_OUTDATED_ORDERS] = ShowOutdatedOrders;
            Settings[HIDE_NOTIFICATIONS_IN_COMBAT] = HideNotificationsInCombat;
            Settings[RAID_BOSSES_ONLY] = RaidBossesOnly;
            Settings[NOTIFIED_OUTDATED_ORDER_IDS] = NotifiedOutdatedOrderIds;
            Settings[CUSTOM_ITEMS] = CustomItems;
            Settings[WATCHLIST_PRICE_ALERTS] = WatchlistPriceAlerts;
            Settings[FINISHED_COLLECTIONS] = FinishedCollections;
            Settings[SECONDARY_API_KEYS] = SecondaryAPIKeys;
            Settings[COMPLETION_CACHES] = CompletionCaches;
            Settings[CHARACTER_CACHES] = CharacterCaches;

            std::ofstream file(SettingsPath);
            file << Settings.dump(1, '\t') << std::endl;

            file.close();
        }
        Settings::Mutex.unlock();
    }

    void ToggleShowWindow(std::filesystem::path SettingsPath)
    {
        ShowWindow = !ShowWindow;

        Save(SettingsPath);
    }

    bool ShowWindow = true;
    std::string ForgeVersion = "0.1.0";
    std::string BackendVersion = "0.1.0";
    std::string ClickerVersion = "0.1.0";
    float EctoRate = 0.90f;
    std::string APIKey;
    bool ScriptsInBackground = true;
    bool ShowOutdatedOrders = true;
    bool HideNotificationsInCombat = true;
    bool RaidBossesOnly = false;
    std::set<int> NotifiedOutdatedOrderIds;
    std::vector<CustomItem> CustomItems;
    std::vector<WatchlistPriceAlert> WatchlistPriceAlerts;
    std::set<std::string> FinishedCollections;
    std::vector<SecondaryAccount> SecondaryAPIKeys;
    std::map<std::string, CompletionCache> CompletionCaches;
    std::map<std::string, std::vector<CharacterInfo>> CharacterCaches;
}
