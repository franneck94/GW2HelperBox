#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

struct Price
{
    int copper;
    int silver;
    int gold;
};

struct MyOrderEntry
{
    int item_id;
    std::string item_name;
    int quantity;
    int my_price;
    int curr_price;
};

struct PriceTriplet
{
    Price buy;
    Price sell;
    Price profit;
};

using OrderedIntValues = std::vector<std::pair<std::string, int>>;
using OrderedStringValues = std::vector<std::pair<std::string, std::string>>;

struct Request
{
    std::string request_id;
    std::future<std::string> future;

    Request(Request &&other) noexcept
        : request_id(std::move(other.request_id)),
          future(std::move(other.future))
    {
    }

    Request(std::string &&request_id, std::future<std::string> &&future) noexcept
        : request_id(std::move(request_id)),
          future(std::move(future))
    {
    }

    Request(const Request &) = delete;
    Request() = delete;
};

struct CustomItem
{
    int item_id = 0;
    std::string name; // user-defined display name shown in the table header instead of the raw item ID
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CustomItem, item_id, name)

struct WatchlistPriceAlert
{
    int item_id = 0;
    int buy_threshold = 0;
    int sell_threshold = 0;
    bool buy_crossed = false;
    bool sell_crossed = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WatchlistPriceAlert, item_id, buy_threshold, sell_threshold, buy_crossed, sell_crossed)

struct SecondaryAccount
{
    std::string name;    // user-defined label shown in the Completions account picker
    std::string api_key; // only used for the Completions tab, never for trading
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SecondaryAccount, name, api_key)

struct MemoryBookmark
{
    std::string name;
    std::uint64_t address = 0;
    bool executable_relative = false;
    int read_size = 256;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MemoryBookmark, name, address, executable_relative, read_size)

struct AccountNote
{
    std::string note;
    std::string last_character;
    std::string last_seen; // local "YYYY-MM-DD HH:MM" of the most recent squad join
    int times_seen = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AccountNote, note, last_character, last_seen, times_seen)

struct SquadMember
{
    std::string account_name;
    std::string character_name;
    int subgroup = 0; // 0 for parties, 1-15 for squads
    bool is_self = false;
    bool is_commander = false;
    bool is_lieutenant = false;
    bool is_in_instance = false;
};

struct CharacterCrafting
{
    std::string discipline;
    int rating = 0;
    bool active = false; // at most two disciplines can be active at a time
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CharacterCrafting, discipline, rating, active)

struct CharacterInfo
{
    std::string name;
    std::string race;
    std::string profession;
    int level = 0;
    long long age_seconds = 0;  // total time played, as reported by the API
    std::string created;        // ISO 8601 creation timestamp
    int deaths = 0;
    int inventory_slots = 0;    // sum of the sizes of all equipped bags
    bool has_inventory = false; // false when the API key is missing the "inventories" scope
    std::vector<CharacterCrafting> crafting;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CharacterInfo, name, race, profession, level, age_seconds,
                                                created, deaths, inventory_slots, has_inventory, crafting)

enum class DailyRaidBounty
{
    ShiverpeaksPass,
    VoiceAndClawOfTheFallen,
    FraenirOfJormag,
    Gorseval,
    Cairn,
    MursaatOverseer,
    AetherbladeHideout,
    CardinalSabir,
    WhisperOfJormag,
    ValeGuardian,
    CosmicObservatory,
    ColdWar,
    Boneskinner,
    Sabetha,
    XunlaiJadeJunkyard,
    TempleOfFebe,
    KeepConstruct,
    Kela,
    Slothasor,
    Matthias,
    Xera,
    Samarog,
    ConjuredAmalgamate,
    TwinLargos,
    Decima,
    CardinalAdina,
    OldLionsCourt,
    Ura,
    KainengOverlook,
    Deimos,
    Qadim,
    QadimThePeerless,
    SoullessHorror,
    HarvestTemple,
    Dhuum,
    Greer,
    Count,
};

struct RaidEvent
{
    std::string id;
    bool is_boss = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RaidEvent, id, is_boss)

struct RaidWing
{
    std::string id;
    std::vector<RaidEvent> events;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RaidWing, id, events)

struct WizardVaultObjective
{
    int id = 0;
    std::string title;
    std::string track;
    int acclaim = 0;
    int progress_current = 0;
    int progress_complete = 0;
    bool claimed = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WizardVaultObjective, id, title, track, acclaim,
                                                progress_current, progress_complete, claimed)

struct CompletionCache
{
    std::vector<RaidWing> raid_wings;
    std::vector<std::pair<std::string, std::vector<std::string>>> dungeon_defs;
    std::vector<std::string> world_bosses;
    std::set<std::string> cleared_raid_events;
    std::set<std::string> cleared_strike_events;
    std::set<std::string> cleared_dungeon_paths;
    std::set<std::string> killed_world_bosses;
    std::vector<WizardVaultObjective> wizard_vault_weekly;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CompletionCache, raid_wings, dungeon_defs, world_bosses,
                                                cleared_raid_events, cleared_strike_events, cleared_dungeon_paths,
                                                killed_world_bosses, wizard_vault_weekly)

struct OutdatedOrderNotification
{
    int item_id;
    std::string item_name;
    int my_price;
    int curr_price;
    std::chrono::steady_clock::time_point created_at;
};

struct PriceAlertNotification
{
    int item_id;
    std::string item_name;
    int current_price;
    int threshold;
    bool is_buy_alert;
    std::chrono::steady_clock::time_point created_at;
};

struct ItemIconState
{
    std::string identifier;
    bool icon_url_requested = false;
    bool image_requested = false;
    bool texture_load_requested = false;
    std::optional<std::future<std::string>> icon_info_future;
    std::optional<std::future<std::string>> image_future;
};

struct NeededRow
{
    std::string name;
    int buy_order;   // highest buy order (copper)
    int instant_buy; // lowest sell listing (copper)
    bool has_price;
};

using CollectionDefinition = std::pair<const std::string, std::vector<std::pair<int, std::string>>>;

struct LockedCollection
{
    const CollectionDefinition *collection;
    std::vector<NeededRow> rows;
    long long total_buy_order = 0;
    long long total_instant_buy = 0;
    bool has_any_price = false;
};
