#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nexus/Nexus.h"

#include "Data.h"
#include "Settings.h"
#include "Types.h"

class Render
{
public:
    constexpr static auto TABLE_HEIGHT_PX = 150.0F;
    constexpr static auto NAME_COLUMN_WIDTH_PX = 140.0F;
    constexpr static auto NUMBER_COLUMN_WIDTH_PX = 25.0F;
    constexpr static auto OFFSET_PX = 40.0F;

    Data data;
    bool &show_window;

    void render();

    Render(bool &show_window) : show_window(show_window) {}

private:
    enum class PriceFilter
    {
        All,
        Gear,
        Craft,
        Runes,
        Forges,
        Sigils,
        Relics,
    };

    int render_table(const std::string &request_id);
    void top_section_child();

    void table_child();
    void render_tables_for_commands(const std::set<std::string> &commands, uint32_t &idx);
    void my_orders_child();
    void material_totals_child();
    void collections_child();
    void delivery_child();
    void calculators_child();
    void settings_child();
    void weekly_child();
    void characters_child();

    int render_custom_item_table(const std::string &request_id, const std::string &display_name);
    void watchlist_child();

    void update_outdated_order_notifications();
    void render_outdated_order_notifications();
    void update_watchlist_price_alerts();
    void render_watchlist_price_alert_notifications();
    Texture *get_item_icon_texture(int item_id);

    std::vector<OutdatedOrderNotification> outdated_order_notifications;
    std::vector<OutdatedOrderNotification> pending_outdated_order_notifications;
    std::map<int, ItemIconState> item_icon_states;
    std::chrono::steady_clock::time_point last_my_orders_poll_time{};
    bool my_orders_polling_started = false;
    bool outdated_order_baseline_initialized = false;
    std::vector<PriceAlertNotification> watchlist_price_alert_notifications;
    std::vector<PriceAlertNotification> pending_watchlist_price_alert_notifications;
    bool watchlist_alerts_evaluated_for_load = false;

    std::optional<std::future<std::string>> account_skins_future;
    std::set<int> unlocked_skins;
    bool account_skins_requested = false;
    bool account_skins_loaded = false;
    std::string account_skins_error;

    std::vector<std::future<std::string>> collection_price_futures;
    std::map<int, std::pair<int, int>> collection_item_prices; // item_id -> {buy_unit, sell_unit} copper
    bool collection_prices_requested = false;
    bool collection_prices_loaded = false;
    bool collections_sort_ascending = true; // sort order for locked collections by total instant-buy price

    PriceFilter price_filter = PriceFilter::All; // active filter for the Prices tab tables

    std::optional<std::future<std::string>> delivery_future;
    long long delivery_coins = 0;
    std::vector<std::pair<int, int>> delivery_items; // {item_id, count} ready to pick up
    bool delivery_requested = false;
    bool delivery_loaded = false;
    std::string delivery_error;

    std::vector<std::future<std::string>> delivery_name_futures;
    std::map<int, std::string> delivery_item_names; // item_id -> name
    bool delivery_names_requested = false;
    bool delivery_names_loaded = false;

    std::optional<std::future<std::string>> raids_def_future;
    std::optional<std::future<std::string>> dungeons_def_future;
    std::optional<std::future<std::string>> account_raids_future;
    std::optional<std::future<std::string>> account_dungeons_future;
    std::optional<std::future<std::string>> worldbosses_def_future;
    std::optional<std::future<std::string>> account_worldbosses_future;
    bool weekly_requested = false;
    bool weekly_loaded = false;
    std::string weekly_error;
    int completions_account_index = 0; // 0 = main account, 1..N = secondary keys
    std::string loaded_completions_account_key;
    std::string weekly_request_account_key;
    std::vector<RaidWing> raid_wings;
    std::vector<std::pair<std::string, std::vector<std::string>>> dungeon_defs; // dungeon id -> path ids
    std::vector<std::string> world_bosses;                                      // all world boss ids
    std::set<std::string> cleared_raid_events;
    std::set<std::string> cleared_dungeon_paths;
    std::set<std::string> killed_world_bosses;

    std::optional<std::future<std::string>> characters_future;
    std::vector<CharacterInfo> characters;
    bool characters_requested = false;
    bool characters_loaded = false;
    std::string characters_error;
    int characters_account_index = 0; // 0 = main account, 1..N = secondary keys
    std::string loaded_characters_account_key;
};
