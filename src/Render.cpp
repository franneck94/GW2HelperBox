#include <windows.h>

#include <shellapi.h>
#include <shlwapi.h>
#include <urlmon.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shlwapi.lib")

#include <DirectXMath.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

#include "httpclient/httpclient.h"
#include "imgui.h"
#include "nlohmann/json.hpp"

#include "API.h"
#include "CollectionSkinIDs.h"
#include "Constants.h"
#include "Data.h"
#include "ItemIDs.h"
#include "Render.h"
#include "RenderUI.h"
#include "Settings.h"
#include "Shared.h"
#include "SquadNotes.h"
#include "Types.h"

using json = nlohmann::json;

namespace
{
    constexpr std::array NON_BOSS_RAID_EVENTS = {
        "bandit_trio",
        "spirit_run",
        "escort",
        "river_of_souls",
        "statues_of_grenth",
        "gate",
        "camp",
    };

    struct StrikeEncounterDefinition
    {
        const char *id;
        const char *name;
    };

    constexpr std::array IBS5_STRIKE_ENCOUNTERS = {
        StrikeEncounterDefinition{"icebrood_construct", "Shiverpeaks Pass"},
        StrikeEncounterDefinition{"voice_and_claw", "Voice and Claw of the Fallen"},
        StrikeEncounterDefinition{"fraenir_of_jormag", "Fraenir of Jormag"},
        StrikeEncounterDefinition{"boneskinner", "Boneskinner"},
        StrikeEncounterDefinition{"whisper_of_jormag", "Whisper of Jormag"},
    };

    constexpr std::array EOD_STRIKE_ENCOUNTERS = {
        StrikeEncounterDefinition{"mai_trin", "Aetherblade Hideout"},
        StrikeEncounterDefinition{"ankka", "Xunlai Jade Junkyard"},
        StrikeEncounterDefinition{"minister_li", "Kaineng Overlook"},
        StrikeEncounterDefinition{"the_dragonvoid", "Harvest Temple"},
    };

    constexpr std::array SOTO_STRIKE_ENCOUNTERS = {
        StrikeEncounterDefinition{"dagda", "Cosmic Observatory"},
        StrikeEncounterDefinition{"cerus", "Temple of Febe"},
    };

    constexpr std::array OTHER_STRIKE_ENCOUNTERS = {
        StrikeEncounterDefinition{"kela", "Kela"},
        StrikeEncounterDefinition{"vloxx", "Vloxx"},
    };

    bool strike_encounter_id_matches(const std::string &event_id, const StrikeEncounterDefinition &encounter)
    {
        if (event_id == encounter.id)
            return true;
        return std::string(encounter.id) == "the_dragonvoid" && event_id == "dragonvoid";
    }

    template <std::size_t Size>
    bool contains_strike_encounter(const std::array<StrikeEncounterDefinition, Size> &encounters,
                                   const std::string &event_id)
    {
        return std::any_of(encounters.begin(), encounters.end(), [&](const StrikeEncounterDefinition &encounter)
                           { return strike_encounter_id_matches(event_id, encounter); });
    }

    bool is_weekly_strike_encounter(const std::string &event_id)
    {
        return contains_strike_encounter(IBS5_STRIKE_ENCOUNTERS, event_id) ||
               contains_strike_encounter(EOD_STRIKE_ENCOUNTERS, event_id) ||
               contains_strike_encounter(SOTO_STRIKE_ENCOUNTERS, event_id) ||
               contains_strike_encounter(OTHER_STRIKE_ENCOUNTERS, event_id);
    }

    constexpr std::array<const char *, static_cast<std::size_t>(DailyRaidBounty::Count)> DAILY_RAID_BOUNTY_NAMES = {
        "Shiverpeaks Pass",
        "Voice and Claw of the Fallen",
        "Fraenir of Jormag",
        "Gorseval",
        "Cairn",
        "Mursaat Overseer",
        "Aetherblade Hideout",
        "Cardinal Sabir",
        "Whisper of Jormag",
        "Vale Guardian",
        "Cosmic Observatory",
        "Cold War",
        "Boneskinner",
        "Sabetha",
        "Xunlai Jade Junkyard",
        "Temple of Febe",
        "Keep Construct",
        "Kela",
        "Slothasor",
        "Matthias",
        "Xera",
        "Samarog",
        "Conjured Amalgamate",
        "Twin Largos",
        "Decima",
        "Cardinal Adina",
        "Old Lion's Court",
        "Ura",
        "Kaineng Overlook",
        "Deimos",
        "Qadim",
        "Qadim the Peerless",
        "Soulless Horror",
        "Harvest Temple",
        "Dhuum",
        "Greer",
    };

    constexpr std::array BOSS_1_DAILY_ROTATION = {
        DailyRaidBounty::ShiverpeaksPass,
        DailyRaidBounty::VoiceAndClawOfTheFallen,
        DailyRaidBounty::FraenirOfJormag,
        DailyRaidBounty::Gorseval,
        DailyRaidBounty::Cairn,
        DailyRaidBounty::MursaatOverseer,
    };

    constexpr std::array BOSS_2_DAILY_ROTATION = {
        DailyRaidBounty::AetherbladeHideout,
        DailyRaidBounty::CardinalSabir,
        DailyRaidBounty::WhisperOfJormag,
        DailyRaidBounty::ValeGuardian,
        DailyRaidBounty::CosmicObservatory,
        DailyRaidBounty::ColdWar,
        DailyRaidBounty::Boneskinner,
        DailyRaidBounty::Sabetha,
        DailyRaidBounty::XunlaiJadeJunkyard,
        DailyRaidBounty::TempleOfFebe,
        DailyRaidBounty::KeepConstruct,
        DailyRaidBounty::Kela,
    };

    constexpr std::array BOSS_3_DAILY_ROTATION = {
        DailyRaidBounty::Slothasor,
        DailyRaidBounty::Matthias,
        DailyRaidBounty::Xera,
        DailyRaidBounty::Samarog,
        DailyRaidBounty::ConjuredAmalgamate,
        DailyRaidBounty::TwinLargos,
        DailyRaidBounty::Decima,
        DailyRaidBounty::CardinalAdina,
        DailyRaidBounty::OldLionsCourt,
        DailyRaidBounty::Ura,
        DailyRaidBounty::KainengOverlook,
        DailyRaidBounty::Deimos,
    };

    constexpr std::array BOSS_4_DAILY_ROTATION = {
        DailyRaidBounty::Qadim,
        DailyRaidBounty::QadimThePeerless,
        DailyRaidBounty::SoullessHorror,
        DailyRaidBounty::HarvestTemple,
        DailyRaidBounty::Dhuum,
        DailyRaidBounty::Greer,
    };

    constexpr const char *daily_raid_bounty_name(DailyRaidBounty bounty)
    {
        return DAILY_RAID_BOUNTY_NAMES[static_cast<std::size_t>(bounty)];
    }

    std::size_t current_daily_raid_rotation_index()
    {
        constexpr auto anchor_date = std::chrono::sys_days{
            std::chrono::year{2026} / std::chrono::month{9} / std::chrono::day{17}};
        constexpr auto anchor_index = 8LL;
        constexpr auto cycle_length = static_cast<long long>(BOSS_2_DAILY_ROTATION.size());

        const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        const auto elapsed_days = (today - anchor_date).count();
        const auto index = (elapsed_days + anchor_index) % cycle_length;
        return static_cast<std::size_t>(index < 0 ? index + cycle_length : index);
    }

    std::array<DailyRaidBounty, 4> current_daily_raid_bounties()
    {
        const auto rotation_index = current_daily_raid_rotation_index();
        return {
            BOSS_1_DAILY_ROTATION[rotation_index % BOSS_1_DAILY_ROTATION.size()],
            BOSS_2_DAILY_ROTATION[rotation_index],
            BOSS_3_DAILY_ROTATION[rotation_index],
            BOSS_4_DAILY_ROTATION[rotation_index % BOSS_4_DAILY_ROTATION.size()],
        };
    }

    std::string normalized_raid_name(std::string value)
    {
        value.erase(std::remove_if(value.begin(), value.end(), [](const unsigned char c)
                                   { return !std::isalnum(c); }),
                    value.end());
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool raid_event_matches_bounty(const std::string &event_id, const DailyRaidBounty bounty)
    {
        if (normalized_raid_name(event_id) == normalized_raid_name(daily_raid_bounty_name(bounty)))
            return true;

        switch (bounty)
        {
        case DailyRaidBounty::ShiverpeaksPass:
            return event_id == "icebrood_construct";
        case DailyRaidBounty::VoiceAndClawOfTheFallen:
            return event_id == "voice_and_claw";
        case DailyRaidBounty::AetherbladeHideout:
            return event_id == "mai_trin";
        case DailyRaidBounty::CosmicObservatory:
            return event_id == "dagda";
        case DailyRaidBounty::XunlaiJadeJunkyard:
            return event_id == "ankka";
        case DailyRaidBounty::TempleOfFebe:
            return event_id == "cerus";
        case DailyRaidBounty::KainengOverlook:
            return event_id == "minister_li";
        case DailyRaidBounty::HarvestTemple:
            return event_id == "the_dragonvoid" || event_id == "dragonvoid";
        default:
            return false;
        }
    }

    void render_daily_raid_rotations()
    {
        const auto current_bounties = current_daily_raid_bounties();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Today's Raid Bounties (UTC reset)");
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("CurrentDailyRaidBountiesTable", 4, flags))
        {
            ImGui::TableSetupColumn("Boss 1", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Boss 2", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Boss 3", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Boss 4", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            for (const auto bounty : current_bounties)
            {
                ImGui::TableNextColumn();
                ImGui::TextWrapped("Raid Bounty: %s", daily_raid_bounty_name(bounty));
            }
            ImGui::EndTable();
        }

        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (!ImGui::CollapsingHeader("Full Daily Raid Bounty Rotation"))
            return;

        if (!ImGui::BeginTable("DailyRaidBountyRotationTable", 4, flags))
            return;

        ImGui::TableSetupColumn("Boss 1", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Boss 2", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Boss 3", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Boss 4", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto render_cell = [](const auto &rotation, std::size_t row, DailyRaidBounty current)
        {
            ImGui::TableNextColumn();
            if (row < rotation.size())
            {
                const auto selected = rotation[row] == current;
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                ImGui::TextWrapped("%zu. Raid Bounty: %s", row + 1, daily_raid_bounty_name(rotation[row]));
                if (selected)
                    ImGui::PopStyleColor();
            }
        };

        for (std::size_t row = 0; row < BOSS_2_DAILY_ROTATION.size(); ++row)
        {
            ImGui::TableNextRow();
            render_cell(BOSS_1_DAILY_ROTATION, row, current_bounties[0]);
            render_cell(BOSS_2_DAILY_ROTATION, row, current_bounties[1]);
            render_cell(BOSS_3_DAILY_ROTATION, row, current_bounties[2]);
            render_cell(BOSS_4_DAILY_ROTATION, row, current_bounties[3]);
        }

        ImGui::EndTable();
    }

    bool copied_name_popup_requested = false;
    auto copied_name_until = std::chrono::steady_clock::now();

    std::string get_url_for_request_id(const std::string &request_id)
    {
        static const std::map<std::string, std::string> url_map = {
            {"rare_gear_salvage", "https://wiki.guildwars2.com/wiki/Piece_of_Rare_Unidentified_Gear/Salvage_Rate"},
            {"gear_salvage", "https://wiki.guildwars2.com/wiki/Piece_of_Unidentified_Gear/Salvage_Rate"},
            {"common_gear_salvage", "https://wiki.guildwars2.com/wiki/Piece_of_Common_Unidentified_Gear/Salvage_Rate"},
            {"lodestone_forge", "https://fast.farming-community.eu/conversions/spirit-shard"},
            {"charm_brilliance_forge", "https://fast.farming-community.eu/conversions/spirit-shard/charm-of-brilliance"},
            {"krait_shield_craft", "https://wiki.guildwars2.com/wiki/Krait_Shell"},
            {"krait_trident_craft", "https://wiki.guildwars2.com/wiki/Krait_Trident"},
            {"krait_focus_craft", "https://wiki.guildwars2.com/wiki/Krait_Star"},
            {"thesis_on_masterful_malice", "https://wiki.guildwars2.com/wiki/Thesis_on_Masterful_Malice"},
            {"symbol_enh_forge", "https://fast.farming-community.eu/conversions/spirit-shard/symbol-of-enhancement"},
            {"scholar_rune", "https://wiki.guildwars2.com/wiki/Superior_Rune_of_the_Scholar"},
            {"guardian_rune", "https://wiki.guildwars2.com/wiki/Superior_Rune_of_the_Guardian"},
            {"dragonhunter_rune", "https://wiki.guildwars2.com/wiki/Superior_Rune_of_the_Dragonhunter"},
            {"relic_of_fireworks", "https://wiki.guildwars2.com/wiki/Relic_of_Fireworks"},
            {"relic_of_thief", "https://wiki.guildwars2.com/wiki/Relic_of_the_Thief"},
            {"relic_of_aristocracy", "https://wiki.guildwars2.com/wiki/Relic_of_the_Aristocracy"},
            {"hard_leather_strap", "https://fast.farming-community.eu/conversions/spirit-shard/charm-of-brilliance"},
            {"sigil_of_impact", "https://wiki.guildwars2.com/wiki/Superior_Sigil_of_Impact"},
            {"sigil_of_doom", "https://wiki.guildwars2.com/wiki/Superior_Sigil_of_Doom"},
            {"sigil_of_torment", "https://wiki.guildwars2.com/wiki/Superior_Sigil_of_Torment"},
            {"sigil_of_bursting", "https://wiki.guildwars2.com/wiki/Superior_Sigil_of_Bursting"},
            {"sigil_of_paralyzation", "https://wiki.guildwars2.com/wiki/Superior_Sigil_of_Paralyzation"}};

        auto it = url_map.find(request_id);
        return (it != url_map.end()) ? it->second : "";
    }

    void open_url_in_browser(const std::string &url)
    {
        if (!url.empty())
        {
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    std::string get_clean_category_name(const std::string &input, const bool skip_last_two)
    {
        auto view = input | std::views::transform(
                                [newWord = true](char c) mutable
                                {
                                    if (c == '_')
                                    {
                                        newWord = true;
                                        return ' ';
                                    }
                                    if (newWord)
                                    {
                                        newWord = false;
                                        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                                    }
                                    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                });

        if (skip_last_two)
            return {view.begin(), view.end() - 2};
        else
            return {view.begin(), view.end()};
    }

    void remove_substring(std::string &str, const std::string &sub)
    {
        size_t pos;
        while ((pos = str.find(sub)) != std::string::npos)
        {
            str.erase(pos, sub.size());
        }
    }

    std::string get_tooltip_for_request(const std::string &request_id, const Data &data)
    {
        const auto it = data.api_string_data.find(request_id);
        if (it == data.api_string_data.end())
            return {};

        const auto it2 = std::find_if(it->second.begin(), it->second.end(), [](const auto &entry)
                                      { return entry.first == "tooltip_str"; });
        return (it2 == it->second.end()) ? std::string{} : it2->second;
    }

    void sort_rows_for_request(std::vector<std::pair<std::string, Price>> &rows, const std::string &request_id)
    {
        const auto to_total_copper = [](const Price &price) -> long long
        {
            return static_cast<long long>(price.gold) * 10000LL + static_cast<long long>(price.silver) * 100LL + price.copper;
        };

        std::stable_sort(rows.begin(), rows.end(), [&](const auto &lhs, const auto &rhs)
                         {
                             const auto lhs_total = to_total_copper(lhs.second);
                             const auto rhs_total = to_total_copper(rhs.second);
                             if (lhs_total == rhs_total)
                                 return lhs.first < rhs.first;

                             return lhs_total < rhs_total; });
    }

    void add_row(const std::string &name, const Price &price, const std::string &tooltip = "")
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text(name.c_str());
        if (ImGui::IsItemHovered() && !tooltip.empty())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip.c_str());
            ImGui::EndTooltip();
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d", price.gold);
        ImGui::TableNextColumn();
        ImGui::Text("%d", price.silver);
        ImGui::TableNextColumn();
        ImGui::Text("%d", price.copper);
    }

    Price copper_to_price(int copper)
    {
        const auto negative = copper < 0;
        const auto abs_copper = std::abs(copper);

        auto price = Price{
            .copper = abs_copper % 100,
            .silver = (abs_copper % 10000) / 100,
            .gold = abs_copper / 10000,
        };

        if (negative)
        {
            price.gold = -price.gold;
            price.silver = -price.silver;
            price.copper = -price.copper;
        }

        return price;
    }

    std::optional<int> get_api_int_value(const OrderedIntValues &values, const std::string &key)
    {
        const auto value = std::find_if(values.begin(), values.end(), [&](const auto &entry)
                                        { return entry.first == key; });
        return value == values.end() ? std::nullopt : std::optional<int>{value->second};
    }

    bool render_price_threshold_input(const char *label, const char *id, int &total_copper)
    {
        auto price = copper_to_price(total_copper);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::PushID(id);

        auto changed = false;
        ImGui::SetNextItemWidth(58.0f);
        changed |= ImGui::InputInt("##Gold", &price.gold, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        ImGui::TextUnformatted("g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(42.0f);
        changed |= ImGui::InputInt("##Silver", &price.silver, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        ImGui::TextUnformatted("s");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(42.0f);
        changed |= ImGui::InputInt("##Copper", &price.copper, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        ImGui::TextUnformatted("c");

        if (changed)
        {
            price.gold = std::clamp(price.gold, 0, 200000);
            price.silver = std::clamp(price.silver, 0, 99);
            price.copper = std::clamp(price.copper, 0, 99);
            total_copper = price.gold * 10000 + price.silver * 100 + price.copper;
        }

        ImGui::PopID();
        return changed;
    }

    void render_my_orders_table(const char *table_id, const std::vector<MyOrderEntry> &orders)
    {
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        if (!ImGui::BeginTable(table_id, 4, flags))
            return;

        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("My Price", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Curr Price", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        if (orders.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("No outdated orders found.");
        }

        for (const auto &order : orders)
        {
            const auto my_price = copper_to_price(order.my_price);
            const auto curr_price = copper_to_price(order.curr_price);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(order.item_name.c_str());
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                ImGui::SetClipboardText(order.item_name.c_str());
                copied_name_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                copied_name_popup_requested = true;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", order.quantity);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%dg %ds %dc", my_price.gold, my_price.silver, my_price.copper);
            ImGui::TableNextColumn();
            ImGui::Text("%dg %ds %dc", curr_price.gold, curr_price.silver, curr_price.copper);
        }

        ImGui::EndTable();
    }

    bool is_tracked_crafting_material(int item_id)
    {
        static const std::set<int> other_materials = {
            ItemIDs::ELDER_WOOD_LOG, ItemIDs::ELDER_WOOD_PLANK,
            ItemIDs::MITHRIL_ORE, ItemIDs::MITHRIL_INGOT};

        static const std::set<int> t5_materials = {
            ItemIDs::LARGE_CLAW, ItemIDs::POTENT_BLOOD, ItemIDs::LARGE_BONE, ItemIDs::INTRICATE_TOTEM,
            ItemIDs::LARGE_FANG, ItemIDs::POTENT_VENOM_SAC, ItemIDs::LARGE_SCALE};

        static const std::set<int> t6_materials = {
            ItemIDs::THICK_LEATHER, ItemIDs::GOSSAMER_SCRAP, ItemIDs::GOSSAMER_THREAD, ItemIDs::SILK_SCRAP,
            ItemIDs::HARDENED_LEATHER, ItemIDs::COARSE_LEATHER, ItemIDs::RUGGED_LEATHER, ItemIDs::ANCIENT_WOOD_LOG,
            ItemIDs::CURED_HARDENED_LEATHER_SQUARE, ItemIDs::ORICHALCUM_ORE, ItemIDs::ECTOPLASM,
            ItemIDs::ELABORATE_TOTEM, ItemIDs::CRYSTALLINE_DUST};

        return other_materials.count(item_id) > 0 || t5_materials.count(item_id) > 0 || t6_materials.count(item_id) > 0;
    }

    void render_my_orders_material_totals_table(const std::vector<MyOrderEntry> &buys, const std::vector<MyOrderEntry> &sells)
    {
        std::map<std::string, int> totals;

        const auto accumulate = [&](const std::vector<MyOrderEntry> &orders)
        {
            for (const auto &order : orders)
                if (is_tracked_crafting_material(order.item_id))
                    totals[order.item_name] += order.quantity;
        };

        accumulate(buys);
        accumulate(sells);

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        if (!ImGui::BeginTable("MyOrdersMaterialTotalsTable", 3, flags))
            return;

        ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Total Ordered", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Stacks", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        if (totals.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("No tracked materials in outdated orders.");
        }

        for (const auto &[name, qty] : totals)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", qty);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", qty / 250.0);
        }

        ImGui::EndTable();
    }

    template <size_t N>
    void _get_ordered_row_data(const std::array<const char *, N> &keys,
                               const std::vector<std::pair<std::string, Price>> &rows,
                               const std::string &tooltip = "")
    {
        for (const auto &row : rows)
        {
            const auto name = get_clean_category_name(row.first, false);
            const auto price = row.second;

            add_row(name, price, tooltip);
        }
    }

    std::vector<std::pair<std::string, Price>> build_rows_for_rendering(const OrderedIntValues &kv)
    {
        auto rows = std::vector<std::pair<std::string, Price>>{};

        if (kv.size() < 3)
            return rows;

        for (size_t i = 0; i + 2 < kv.size();)
        {
            if (kv[i].first.find("_g") == std::string::npos && kv[i].first.find("_s") == std::string::npos && kv[i].first.find("_c") == std::string::npos)
            {
                ++i;
                continue;
            }

            if (i + 2 >= kv.size())
                break;

            const auto &name0 = kv[i].first;
            const auto &name1 = kv[i + 1].first;
            const auto &name2 = kv[i + 2].first;

            const auto val0 = kv[i].second;
            const auto val1 = kv[i + 1].second;
            const auto val2 = kv[i + 2].second;

            const auto gold = name0.ends_with("_g") ? val0 : name1.ends_with("_g") ? val1
                                                                                   : val2;
            const auto silver = name0.ends_with("_s") ? val0 : name1.ends_with("_s") ? val1
                                                                                     : val2;
            const auto copper = name0.ends_with("_c") ? val0 : name1.ends_with("_c") ? val1
                                                                                     : val2;

            const auto transformed_name = std::string{name0.substr(0, name0.size() - 2)};

            const auto price = Price{
                .copper = copper,
                .silver = silver,
                .gold = gold,
            };

            rows.emplace_back(transformed_name, price);
            i += 3;
        }

        return rows;
    }

    void render_rows_for_request(std::vector<std::pair<std::string, Price>> &rows,
                                 const std::string &request_id,
                                 const Data &data)
    {
        const auto tooltip = get_tooltip_for_request(request_id, data);

        if (request_id == "thesis_on_masterful_malice")
            _get_ordered_row_data(API::THESIS_MASTERFUL_MALICE, rows, tooltip);
        // runes
        else if (request_id == "scholar_rune")
            _get_ordered_row_data(API::SCHOLAR_RUNE_NAMES, rows, tooltip);
        else if (request_id == "dragonhunter_rune")
            _get_ordered_row_data(API::DRAGONHUNTER_RUNE_NAMES, rows, tooltip);
        else if (request_id == "guardian_rune")
            _get_ordered_row_data(API::GUARDIAN_RUNE_NAMES, rows, tooltip);
        // relics
        else if (request_id == "relic_of_fireworks")
            _get_ordered_row_data(API::FIREWORKS_NAMES, rows, tooltip);
        else if (request_id == "relic_of_thief")
            _get_ordered_row_data(API::THIEF_NAMES, rows, tooltip);
        else if (request_id == "relic_of_aristocracy")
            _get_ordered_row_data(API::ARISTOCRACY_NAMES, rows, tooltip);
        // sigil
        else if (request_id == "sigil_of_impact")
            _get_ordered_row_data(API::SIGIL_OF_IMPACT_NAMES, rows, tooltip);
        else if (request_id == "sigil_of_torment")
            _get_ordered_row_data(API::SIGIL_OF_TORMENT_NAMES, rows, tooltip);
        else if (request_id == "sigil_of_doom")
            _get_ordered_row_data(API::SIGIL_OF_DOOM_NAMES, rows, tooltip);
        else if (request_id == "sigil_of_bursting")
            _get_ordered_row_data(API::SIGIL_OF_BURSTING_NAMES, rows, tooltip);
        else if (request_id == "sigil_of_paralyzation")
            _get_ordered_row_data(API::SIGIL_OF_PARALYZATION_NAMES, rows, tooltip);
        // gear
        else if (request_id == "krait_shield_craft")
            _get_ordered_row_data(API::KRAIT_SHIELD_CRAFT_NAMES, rows, tooltip);
        else if (request_id == "krait_trident_craft")
            _get_ordered_row_data(API::KRAIT_TRIDENT_CRAFT_NAMES, rows, tooltip);
        else if (request_id == "krait_focus_craft")
            _get_ordered_row_data(API::KRAIT_FOCUS_CRAFT_NAMES, rows, tooltip);
        else if (request_id == "rare_gear_salvage")
            _get_ordered_row_data(API::RARE_GEAR_NAMES, rows, tooltip);
        // gear
        else if (request_id == "gear_salvage")
            _get_ordered_row_data(API::GEAR_SALVAGE_NAMES, rows, tooltip);
        else if (request_id == "common_gear_salvage")
            _get_ordered_row_data(API::COMMON_GEAR_NAMES, rows, tooltip);
        // t5
        else if (request_id == "t5_mats_buy")
        {
            sort_rows_for_request(rows, request_id);
            _get_ordered_row_data(API::T5_MATS_BUY_NAMES, rows, tooltip);
        }
        else if (request_id == "mats_crafting_compare")
        {
            sort_rows_for_request(rows, request_id);
            _get_ordered_row_data(API::MATS_CRAFTING_COMPARE_NAMES, rows, tooltip);
        }
        // forge
        else if (request_id == "symbol_enh_forge")
            _get_ordered_row_data(API::FORGE_ENH_NAMES, rows, tooltip);
        else if (request_id == "charm_brilliance_forge")
            _get_ordered_row_data(API::FORGE_CHARM_NAMES, rows, tooltip);
        else if (request_id == "lodestone_forge")
            _get_ordered_row_data(API::LODESTONE_NAMES, rows, tooltip);
        else if (request_id == "ecto" || request_id == "rare_gear")
        {
            for (const auto &[name, price] : rows)
                add_row(name, price);
        }
    }

    void get_row_data(const OrderedIntValues &kv, const std::string &request_id, const Data &data)
    {
        auto rows = build_rows_for_rendering(kv);
        render_rows_for_request(rows, request_id, data);
    }

}

int Render::render_table(const std::string &request_id)
{
    const auto &kv = data.api_data[request_id];
    if (API::COMMANDS.find(request_id) == API::COMMANDS.end() || data.api_data.find(request_id) == data.api_data.end() || data.api_data[request_id].empty())
    {
        ImGui::Text("No data yet for %s", request_id.c_str());

        return -1;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders;
    if (ImGui::BeginTable(("Prices##" + request_id).c_str(), 4, flags))
    {
        const std::string url = get_url_for_request_id(request_id);
        RenderUI::render_table_header(request_id, url);

        if (API::COMMANDS.find(request_id) != API::COMMANDS.end())
            get_row_data(kv, request_id, data);

        ImGui::EndTable();
    }

    return static_cast<int>(ImGui::GetCursorPosY());
}

int Render::render_custom_item_table(const std::string &request_id, const std::string &display_name)
{
    const auto it = data.api_data.find(request_id);
    if (it == data.api_data.end() || it->second.empty())
    {
        ImGui::Text("No data yet for item %s", display_name.c_str());

        return -1;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders;
    if (ImGui::BeginTable(("Prices##" + request_id).c_str(), 4, flags))
    {
        RenderUI::render_table_header(display_name, "", true);

        const auto rows = build_rows_for_rendering(it->second);
        for (const auto &[name, price] : rows)
            add_row(name, price);

        ImGui::EndTable();
    }

    return static_cast<int>(ImGui::GetCursorPosY());
}

void Render::top_section_child()
{
    if (!show_window)
        return;

    static auto last_refresh_time = std::chrono::steady_clock::now();
    static bool server_started = false;

    if (!server_started)
    {
        const auto server_exe_path = (Globals::AddonPath / "GW2TP_Python.exe").string();
        if (std::filesystem::exists(server_exe_path))
        {
            RenderUI::start_executable(server_exe_path, "server.exe", false);
            Globals::GW2TPServerProcessActive = true;
            (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Auto-started localhost server on first render");
            server_started = true;
        }
    }

    const auto window_width = ImGui::GetWindowContentRegionWidth();
    ImGui::BeginChild("TopSection", ImVec2(window_width, 64.0f), false, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    RenderUI::render_top_controls(data, last_refresh_time);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::EndChild();
}

void Render::render_tables_for_commands(const std::set<std::string> &commands, uint32_t &idx)
{
    const auto window_width = ImGui::GetWindowContentRegionWidth();
    const auto large_window = window_width > 450.0F;
    const auto very_large_window = window_width > 750.0F;
    const auto child_size = ImVec2(window_width * (very_large_window ? 0.33f : (large_window ? 0.5f : 1.0F)), TABLE_HEIGHT_PX);

    for (const auto command : commands)
    {
        const auto category_it = API::COMMAND_CATEGORY.find(command);
        const auto category = category_it != API::COMMAND_CATEGORY.end() ? category_it->second : API::PriceCategory::Other;
        const auto matches_filter = price_filter == PriceFilter::All ||
                                    (price_filter == PriceFilter::Gear && category == API::PriceCategory::Gear) ||
                                    (price_filter == PriceFilter::Craft && category == API::PriceCategory::Craft) ||
                                    (price_filter == PriceFilter::Runes && category == API::PriceCategory::Rune) ||
                                    (price_filter == PriceFilter::Forges && category == API::PriceCategory::Forge) ||
                                    (price_filter == PriceFilter::Sigils && category == API::PriceCategory::Sigil) ||
                                    (price_filter == PriceFilter::Relics && category == API::PriceCategory::Relic);
        if (!matches_filter)
            continue;

        ImGui::BeginChild(("tableChild" + std::to_string(idx)).c_str(), child_size, false, ImGuiWindowFlags_AlwaysAutoResize);
        const auto table_height = render_table(command);
        ImGui::EndChild();
        if (very_large_window && (idx % 3 != 2))
            ImGui::SameLine();
        else if (!very_large_window && large_window && (idx % 2 == 0))
            ImGui::SameLine();
        ++idx;
    }
}

void Render::table_child()
{
    const auto window_width = ImGui::GetWindowContentRegionWidth();

    constexpr auto filter_width = 160.0f;
    const char *filter_labels[] = {"All", "Gear", "Craft", "Runes", "Forges", "Sigils", "Relics"};
    auto filter_index = static_cast<int>(price_filter);
    ImGui::SetCursorPosX(max(0.0f, (window_width - filter_width) * 0.5f));
    ImGui::SetNextItemWidth(filter_width);
    if (ImGui::Combo("##PriceFilter", &filter_index, filter_labels, IM_ARRAYSIZE(filter_labels)))
        price_filter = static_cast<PriceFilter>(filter_index);

    ImGui::Spacing();

    ImGui::BeginChild("ScrollableContent", ImVec2(window_width, -1.0), false, ImGuiWindowFlags_AlwaysAutoResize);

    auto idx = 0U;
    render_tables_for_commands(API::CRAFT_COMMANDS, idx);
    render_tables_for_commands(API::GEAR_COMMANDS, idx);
    render_tables_for_commands(API::FORGE_COMMANDS, idx);
    render_tables_for_commands(API::RUNE_COMMANDS, idx);
    render_tables_for_commands(API::SIGIL_COMMANDS, idx);
    render_tables_for_commands(API::RELIC_COMMANDS, idx);

    ImGui::EndChild();
}

void Render::my_orders_child()
{
    if (!data.loaded)
    {
        ImGui::TextUnformatted("Loading price data before requesting orders...");
        return;
    }

    if (!data.my_orders_requested)
        data.request_my_orders();
    data.store_my_orders();

    if (ImGui::Button("Refresh My Orders"))
    {
        data.my_orders_requested = false;
        data.request_my_orders();
    }

    ImGui::SameLine();
    if (!data.my_orders_loaded)
        ImGui::TextUnformatted("Loading...");
    else if (!data.my_orders_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", data.my_orders_error.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(("Buy Orders (Not Highest Bid) (" + std::to_string(data.my_orders_buys.size()) + ")###MyOrdersBuysHeader").c_str()))
        render_my_orders_table("MyOrdersBuysTable", data.my_orders_buys);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(("Sell Orders (Not Lowest Sell) (" + std::to_string(data.my_orders_sells.size()) + ")###MyOrdersSellsHeader").c_str()))
        render_my_orders_table("MyOrdersSellsTable", data.my_orders_sells);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(("Buy Orders (Highest Bid) (" + std::to_string(data.my_orders_buys_current.size()) + ")###MyOrdersBuysCurrentHeader").c_str()))
        render_my_orders_table("MyOrdersBuysCurrentTable", data.my_orders_buys_current);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(("Sell Orders (Lowest Sell) (" + std::to_string(data.my_orders_sells_current.size()) + ")###MyOrdersSellsCurrentHeader").c_str()))
        render_my_orders_table("MyOrdersSellsCurrentTable", data.my_orders_sells_current);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Tracked Material Totals (Elder Wood, Mithril, T5 & T6 Mats)###MyOrdersMaterialTotalsHeader"))
        render_my_orders_material_totals_table(data.my_orders_buys, data.my_orders_sells);

    if (copied_name_popup_requested)
    {
        ImGui::OpenPopup("NameCopiedPopup");
        copied_name_popup_requested = false;
    }

    if (ImGui::BeginPopup("NameCopiedPopup", ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Name copied");
        if (std::chrono::steady_clock::now() >= copied_name_until)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Render::weekly_child()
{
    /* main account first, then any secondary keys the user added in Settings */
    std::vector<std::pair<std::string, std::string>> accounts;
    accounts.emplace_back("Main", Settings::APIKey);
    for (const auto &account : Settings::SecondaryAPIKeys)
        accounts.emplace_back(account.name, account.api_key);

    const auto raid_event_counts = [](const RaidEvent &event)
    { return (!Settings::RaidBossesOnly || event.is_boss) && !is_weekly_strike_encounter(event.id); };
    const auto raid_wing_has_counted_events = [&](const RaidWing &wing)
    { return std::any_of(wing.events.begin(), wing.events.end(), raid_event_counts); };
    const auto raid_wing_complete = [&](const RaidWing &wing)
    {
        const auto relevant_event = std::find_if(wing.events.begin(), wing.events.end(), raid_event_counts);
        return relevant_event != wing.events.end() &&
               std::all_of(wing.events.begin(), wing.events.end(), [&](const RaidEvent &event)
                           { return !raid_event_counts(event) || cleared_raid_events.count(event.id) > 0; });
    };

    if (ImGui::Checkbox("Bosses only", &Settings::RaidBossesOnly))
        Settings::Save(Globals::SettingsPath);

    if (completions_account_index >= static_cast<int>(accounts.size()))
        completions_account_index = 0;

    ImGui::TextUnformatted("Account");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    const auto all_raid_wings_complete = weekly_loaded && weekly_error.empty() &&
                                         std::any_of(raid_wings.begin(), raid_wings.end(), raid_wing_has_counted_events) &&
                                         std::all_of(raid_wings.begin(), raid_wings.end(), [&](const RaidWing &wing)
                                                     { return !raid_wing_has_counted_events(wing) || raid_wing_complete(wing); });
    if (all_raid_wings_complete)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
    const auto account_combo_open = ImGui::BeginCombo("##CompletionsAccount", accounts[completions_account_index].first.c_str());
    if (all_raid_wings_complete)
        ImGui::PopStyleColor();
    if (account_combo_open)
    {
        for (auto account_index = 0; account_index < static_cast<int>(accounts.size()); ++account_index)
        {
            const auto selected = completions_account_index == account_index;
            if (ImGui::Selectable(accounts[account_index].first.c_str(), selected) && !selected && !weekly_requested)
            {
                completions_account_index = account_index;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const auto &selected_key = accounts[completions_account_index].second;

    if (loaded_completions_account_key != selected_key)
    {
        loaded_completions_account_key = selected_key;
        weekly_loaded = false;
        weekly_error.clear();
        wizard_vault_weekly_error.clear();
        raid_wings.clear();
        dungeon_defs.clear();
        world_bosses.clear();
        cleared_raid_events.clear();
        cleared_dungeon_paths.clear();
        killed_world_bosses.clear();
        wizard_vault_weekly.clear();

        const auto cached = Settings::CompletionCaches.find(selected_key);
        if (cached != Settings::CompletionCaches.end())
        {
            raid_wings = cached->second.raid_wings;
            dungeon_defs = cached->second.dungeon_defs;
            world_bosses = cached->second.world_bosses;
            cleared_raid_events = cached->second.cleared_raid_events;
            cleared_dungeon_paths = cached->second.cleared_dungeon_paths;
            killed_world_bosses = cached->second.killed_world_bosses;
            wizard_vault_weekly = cached->second.wizard_vault_weekly;
            weekly_loaded = true;
        }
    }

    if (selected_key.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Set an API key for this account in the Settings tab to check its completions.");
        return;
    }

    if (ImGui::Button("Refresh") && !weekly_requested)
    {
        const auto token = std::wstring(selected_key.begin(), selected_key.end());
        raids_def_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/raids?ids=all");
        dungeons_def_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/dungeons?ids=all");
        worldbosses_def_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/worldbosses");
        account_raids_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/account/raids?access_token=" + token);
        account_dungeons_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/account/dungeons?access_token=" + token);
        account_worldbosses_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/account/worldbosses?access_token=" + token);
        wizard_vault_weekly_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/account/wizardsvault/weekly?access_token=" + token);
        weekly_request_account_key = selected_key;
        weekly_requested = true;
        weekly_error.clear();
        wizard_vault_weekly_error.clear();
    }

    if (weekly_requested && raids_def_future.has_value() && dungeons_def_future.has_value() &&
        worldbosses_def_future.has_value() && account_raids_future.has_value() &&
        account_dungeons_future.has_value() && account_worldbosses_future.has_value() &&
        wizard_vault_weekly_future.has_value())
    {
        const auto ready = [](std::optional<std::future<std::string>> &f)
        { return f->wait_for(std::chrono::seconds(0)) == std::future_status::ready; };

        if (ready(raids_def_future) && ready(dungeons_def_future) && ready(worldbosses_def_future) &&
            ready(account_raids_future) && ready(account_dungeons_future) && ready(account_worldbosses_future) &&
            ready(wizard_vault_weekly_future))
        {
            try
            {
                CompletionCache refreshed;
                std::string refresh_error;
                for (const auto &raid : json::parse(raids_def_future->get()))
                    for (const auto &wing : raid.value("wings", json::array()))
                    {
                        RaidWing raid_wing{wing.value("id", std::string{}), {}};
                        for (const auto &event : wing.value("events", json::array()))
                        {
                            const auto event_id = event.value("id", std::string{});
                            const auto is_boss = event.value("type", std::string{}) == "Boss" &&
                                                 std::ranges::find(NON_BOSS_RAID_EVENTS, event_id) == NON_BOSS_RAID_EVENTS.end();
                            if (!event_id.empty())
                                raid_wing.events.push_back(RaidEvent{event_id, is_boss});
                        }
                        if (!raid_wing.events.empty())
                            refreshed.raid_wings.push_back(std::move(raid_wing));
                    }

                for (const auto &dungeon : json::parse(dungeons_def_future->get()))
                {
                    std::vector<std::string> paths;
                    for (const auto &path : dungeon.value("paths", json::array()))
                        paths.push_back(path.value("id", std::string{}));
                    refreshed.dungeon_defs.emplace_back(dungeon.value("id", std::string{}), std::move(paths));
                }

                for (const auto &id : json::parse(worldbosses_def_future->get()))
                    refreshed.world_bosses.push_back(id.get<std::string>());

                const auto acc_raids = json::parse(account_raids_future->get());
                if (acc_raids.is_array())
                    for (const auto &id : acc_raids)
                        refreshed.cleared_raid_events.insert(id.get<std::string>());
                else if (acc_raids.contains("text"))
                    refresh_error = acc_raids["text"].get<std::string>();

                const auto acc_dungeons = json::parse(account_dungeons_future->get());
                if (acc_dungeons.is_array())
                    for (const auto &id : acc_dungeons)
                        refreshed.cleared_dungeon_paths.insert(id.get<std::string>());
                else if (acc_dungeons.contains("text") && refresh_error.empty())
                    refresh_error = acc_dungeons["text"].get<std::string>();

                const auto acc_worldbosses = json::parse(account_worldbosses_future->get());
                if (acc_worldbosses.is_array())
                    for (const auto &id : acc_worldbosses)
                        refreshed.killed_world_bosses.insert(id.get<std::string>());
                else if (acc_worldbosses.contains("text") && refresh_error.empty())
                    refresh_error = acc_worldbosses["text"].get<std::string>();

                const auto vault_weekly = json::parse(wizard_vault_weekly_future->get());
                if (vault_weekly.is_object() && vault_weekly.contains("objectives") && vault_weekly["objectives"].is_array())
                {
                    for (const auto &objective : vault_weekly["objectives"])
                    {
                        refreshed.wizard_vault_weekly.push_back(WizardVaultObjective{
                            .id = objective.value("id", 0),
                            .title = objective.value("title", std::string{}),
                            .track = objective.value("track", std::string{}),
                            .acclaim = objective.value("acclaim", 0),
                            .progress_current = objective.value("progress_current", 0),
                            .progress_complete = objective.value("progress_complete", 0),
                            .claimed = objective.value("claimed", false),
                        });
                    }
                }
                else
                {
                    wizard_vault_weekly_error = vault_weekly.value("text", "Unexpected Wizard's Vault response.");
                    const auto cached = Settings::CompletionCaches.find(weekly_request_account_key);
                    if (cached != Settings::CompletionCaches.end())
                        refreshed.wizard_vault_weekly = cached->second.wizard_vault_weekly;
                }

                if (refresh_error.empty())
                {
                    Settings::CompletionCaches[weekly_request_account_key] = refreshed;
                    Settings::Save(Globals::SettingsPath);

                    if (selected_key == weekly_request_account_key)
                    {
                        raid_wings = std::move(refreshed.raid_wings);
                        dungeon_defs = std::move(refreshed.dungeon_defs);
                        world_bosses = std::move(refreshed.world_bosses);
                        cleared_raid_events = std::move(refreshed.cleared_raid_events);
                        cleared_dungeon_paths = std::move(refreshed.cleared_dungeon_paths);
                        killed_world_bosses = std::move(refreshed.killed_world_bosses);
                        wizard_vault_weekly = std::move(refreshed.wizard_vault_weekly);
                        weekly_loaded = true;
                    }
                }
                else if (selected_key == weekly_request_account_key)
                    weekly_error = std::move(refresh_error);
            }
            catch (const std::exception &e)
            {
                if (selected_key == weekly_request_account_key)
                    weekly_error = std::string("Failed to parse response: ") + e.what();
            }

            raids_def_future.reset();
            dungeons_def_future.reset();
            worldbosses_def_future.reset();
            account_raids_future.reset();
            account_dungeons_future.reset();
            account_worldbosses_future.reset();
            wizard_vault_weekly_future.reset();
            weekly_requested = false;
            weekly_request_account_key.clear();
        }
    }

    ImGui::SameLine();
    if (weekly_requested)
        ImGui::TextUnformatted("Loading...");
    else if (!weekly_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", weekly_error.c_str());
    else if (!weekly_loaded)
        ImGui::TextUnformatted("No saved completion data. Click Refresh to load it.");

    if (!weekly_loaded)
        return;

    const auto pretty = [](const std::string &id)
    {
        std::string out;
        auto new_word = true;
        for (const char c : id)
        {
            if (c == '_')
            {
                out += ' ';
                new_word = true;
            }
            else
            {
                out += new_word ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                new_word = false;
            }
        }
        return out;
    };

    const auto render_group = [&](const char *heading,
                                  const std::vector<std::pair<std::string, std::vector<std::string>>> &defs,
                                  const std::set<std::string> &cleared,
                                  const char *id_prefix)
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", heading);
        ImGui::Separator();

        for (const auto &[group_id, entries] : defs)
        {
            auto done_count = 0;
            for (const auto &entry : entries)
                if (cleared.count(entry) > 0)
                    ++done_count;

            const auto header = pretty(group_id) + " (" + std::to_string(done_count) + "/" + std::to_string(entries.size()) +
                                ")###" + id_prefix + group_id;
            if (!ImGui::CollapsingHeader(header.c_str()))
                continue;

            for (const auto &entry : entries)
            {
                const auto done = cleared.count(entry) > 0;
                ImGui::TextColored(done ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                   "%s %s", done ? "[x]" : "[ ]", pretty(entry).c_str());
            }
        }
    };

    ImGui::Spacing();
    const auto vault_completed = std::count_if(wizard_vault_weekly.begin(), wizard_vault_weekly.end(), [](const WizardVaultObjective &objective)
                                               { return objective.progress_complete > 0 && objective.progress_current >= objective.progress_complete; });
    const auto vault_claimed = std::count_if(wizard_vault_weekly.begin(), wizard_vault_weekly.end(), [](const WizardVaultObjective &objective)
                                             { return objective.claimed; });
    const auto vault_header = "Wizard's Vault Weekly (" + std::to_string(vault_completed) + "/" +
                              std::to_string(wizard_vault_weekly.size()) + " complete, " +
                              std::to_string(vault_claimed) + " claimed)###WizardVaultWeeklyHeader";
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
    const auto vault_expanded = ImGui::CollapsingHeader(vault_header.c_str());
    ImGui::PopStyleColor();
    if (vault_expanded)
    {
        ImGui::Separator();
        if (!wizard_vault_weekly_error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", wizard_vault_weekly_error.c_str());
        else if (wizard_vault_weekly.empty())
            ImGui::TextDisabled("No weekly objective data. Click Refresh to load it.");

        constexpr ImGuiTableFlags vault_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
        if (!wizard_vault_weekly.empty() && ImGui::BeginTable("WizardVaultWeeklyTable", 5, vault_flags))
        {
            ImGui::TableSetupColumn("Objective", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableSetupColumn("AA", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 75.0f);
            ImGui::TableHeadersRow();

            for (const auto &objective : wizard_vault_weekly)
            {
                const auto complete = objective.progress_complete > 0 && objective.progress_current >= objective.progress_complete;
                const auto color = objective.claimed ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                                     : complete ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                                                : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(color, "%s", objective.title.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(objective.track.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d/%d", objective.progress_current, objective.progress_complete);
                ImGui::TableNextColumn();
                ImGui::Text("%d", objective.acclaim);
                ImGui::TableNextColumn();
                ImGui::TextColored(color, "%s", objective.claimed ? "Claimed" : complete ? "Ready" : "In progress");
            }

            ImGui::EndTable();
        }
    }

    const auto current_daily_bounties = current_daily_raid_bounties();
    const auto strike_is_cleared = [&](const StrikeEncounterDefinition &encounter)
    {
        return std::any_of(cleared_raid_events.begin(), cleared_raid_events.end(), [&](const std::string &event_id)
                           { return strike_encounter_id_matches(event_id, encounter); });
    };
    const auto strike_is_daily = [&](const StrikeEncounterDefinition &encounter)
    {
        return std::any_of(current_daily_bounties.begin(), current_daily_bounties.end(), [&](const DailyRaidBounty bounty)
                           { return raid_event_matches_bounty(encounter.id, bounty); });
    };
    const auto render_strike_category = [&](const char *name, const auto &encounters)
    {
        const auto done_count = std::count_if(encounters.begin(), encounters.end(), strike_is_cleared);
        const auto has_daily = std::any_of(encounters.begin(), encounters.end(), strike_is_daily);
        const auto header = std::string(name) + " (" + std::to_string(done_count) + "/" +
                            std::to_string(encounters.size()) + ")###strike_" + name;
        if (has_daily)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        const auto expanded = ImGui::CollapsingHeader(header.c_str());
        if (has_daily)
            ImGui::PopStyleColor();
        if (!expanded)
            return;

        for (const auto &encounter : encounters)
        {
            const auto done = strike_is_cleared(encounter);
            const auto daily = strike_is_daily(encounter);
            ImGui::TextColored(daily ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                     : done ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                            : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "%s %s", done ? "[x]" : "[ ]", encounter.name);
        }
    };

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Strike Raid Encounters (weekly reset)");
    ImGui::Separator();
    render_strike_category("IBS5", IBS5_STRIKE_ENCOUNTERS);
    render_strike_category("EOD", EOD_STRIKE_ENCOUNTERS);
    render_strike_category("SOTO", SOTO_STRIKE_ENCOUNTERS);
    render_strike_category("Others", OTHER_STRIKE_ENCOUNTERS);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Raids (weekly reset)");
    ImGui::Separator();
    const auto event_is_daily = [&](const RaidEvent &event)
    {
        return std::any_of(current_daily_bounties.begin(), current_daily_bounties.end(), [&](const DailyRaidBounty bounty)
                           { return raid_event_matches_bounty(event.id, bounty); });
    };
    for (const auto &wing : raid_wings)
    {
        auto done_count = 0;
        auto event_count = 0;
        for (const auto &event : wing.events)
        {
            if (!raid_event_counts(event))
                continue;
            ++event_count;
            if (cleared_raid_events.count(event.id) > 0)
                ++done_count;
        }

        if (event_count == 0)
            continue;

        const auto complete = raid_wing_complete(wing);
        const auto daily = std::any_of(wing.events.begin(), wing.events.end(), event_is_daily);
        const auto header = pretty(wing.id) + " (" + std::to_string(done_count) + "/" + std::to_string(event_count) +
                            ")###raid_" + wing.id;
        if (daily || complete)
            ImGui::PushStyleColor(ImGuiCol_Text, daily ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                                       : ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        const auto expanded = ImGui::CollapsingHeader(header.c_str());
        if (daily || complete)
            ImGui::PopStyleColor();
        if (!expanded)
            continue;

        for (const auto &event : wing.events)
        {
            const auto done = cleared_raid_events.count(event.id) > 0;
            const auto daily_bounty = event_is_daily(event);
            ImGui::TextColored(daily_bounty ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                                            : done ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                                   : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "%s %s", done ? "[x]" : "[ ]", pretty(event.id).c_str());
        }
    }

    render_daily_raid_rotations();

    render_group("Dungeons (daily reset)", dungeon_defs, cleared_dungeon_paths, "dungeon_");

    ImGui::Spacing();
    {
        auto killed_count = 0;
        for (const auto &boss : world_bosses)
            if (killed_world_bosses.count(boss) > 0)
                ++killed_count;

        const auto header = "World Bosses (daily reset) (" + std::to_string(killed_count) + "/" +
                            std::to_string(world_bosses.size()) + ")###worldbosses";
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "World Bosses (daily reset)");
        ImGui::Separator();
        if (ImGui::CollapsingHeader(header.c_str()))
            for (const auto &boss : world_bosses)
            {
                const auto done = killed_world_bosses.count(boss) > 0;
                ImGui::TextColored(done ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                   "%s %s", done ? "[x]" : "[ ]", pretty(boss).c_str());
            }
    }
}

void Render::characters_child()
{
    /* main account first, then any secondary keys the user added in Settings */
    std::vector<std::pair<std::string, std::string>> accounts;
    accounts.emplace_back("Main", Settings::APIKey);
    for (const auto &account : Settings::SecondaryAPIKeys)
        accounts.emplace_back(account.name, account.api_key);

    if (characters_account_index >= static_cast<int>(accounts.size()))
        characters_account_index = 0;

    ImGui::TextUnformatted("Account");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##CharactersAccount", accounts[characters_account_index].first.c_str()))
    {
        for (auto account_index = 0; account_index < static_cast<int>(accounts.size()); ++account_index)
        {
            const auto selected = characters_account_index == account_index;
            if (ImGui::Selectable(accounts[account_index].first.c_str(), selected) && !selected && !characters_requested)
                characters_account_index = account_index;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const auto &selected_key = accounts[characters_account_index].second;
    const auto request_characters = [&]()
    {
        const auto url = L"https://api.guildwars2.com/v2/characters?ids=all&access_token=" +
                         std::wstring(selected_key.begin(), selected_key.end());
        characters_future = HTTPClient::GetRequestAsync(url);
        characters_requested = true;
        characters_error.clear();
    };

    if (loaded_characters_account_key != selected_key)
    {
        loaded_characters_account_key = selected_key;
        characters.clear();
        characters_loaded = false;
        characters_requested = false;
        characters_error.clear();
        characters_future.reset();

        const auto cached = Settings::CharacterCaches.find(selected_key);
        if (cached != Settings::CharacterCaches.end())
        {
            characters = cached->second;
            characters_loaded = true;
        }
        else if (!selected_key.empty())
            request_characters();
    }

    if (selected_key.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Set an API key for this account in the Settings tab to list its characters.");
        return;
    }

    if (ImGui::Button("Refresh") && !characters_requested)
        request_characters();

    ImGui::SameLine();
    if (characters_requested)
        ImGui::TextUnformatted("Loading...");
    else if (!characters_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", characters_error.c_str());

    if (characters_future.has_value() &&
        characters_future->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            const auto j = json::parse(characters_future->get());
            if (j.is_array())
            {
                std::vector<CharacterInfo> refreshed_characters;
                for (const auto &entry : j)
                {
                    CharacterInfo info;
                    info.name = entry.value("name", std::string{});
                    info.race = entry.value("race", std::string{});
                    info.profession = entry.value("profession", std::string{});
                    info.level = entry.value("level", 0);
                    info.age_seconds = entry.value("age", 0LL);
                    info.created = entry.value("created", std::string{});
                    info.deaths = entry.value("deaths", 0);

                    /* "bags" is only sent when the key carries the inventories scope */
                    if (entry.contains("bags") && entry["bags"].is_array())
                    {
                        info.has_inventory = true;
                        for (const auto &bag : entry["bags"])
                            if (!bag.is_null())
                                info.inventory_slots += bag.value("size", 0);
                    }

                    for (const auto &craft : entry.value("crafting", json::array()))
                        info.crafting.push_back(CharacterCrafting{craft.value("discipline", std::string{}),
                                                                  craft.value("rating", 0),
                                                                  craft.value("active", false)});

                    refreshed_characters.push_back(std::move(info));
                }

                characters = std::move(refreshed_characters);
                characters_loaded = true;
                Settings::CharacterCaches[selected_key] = characters;
                Settings::Save(Globals::SettingsPath);
            }
            else if (j.contains("text"))
                characters_error = j["text"].get<std::string>();
            else
                characters_error = "Unexpected response from GW2 API.";
        }
        catch (const std::exception &e)
        {
            characters_error = std::string("Failed to parse response: ") + e.what();
        }
        characters_future.reset();
        characters_requested = false;
    }

    if (!characters_loaded)
        return;

    auto total_age = 0LL;
    auto total_deaths = 0LL;
    auto missing_inventory_scope = false;
    for (const auto &info : characters)
    {
        total_age += info.age_seconds;
        total_deaths += info.deaths;
        missing_inventory_scope = missing_inventory_scope || !info.has_inventory;
    }

    const auto deaths_per_hour = [](long long deaths, long long age_seconds)
    {
        return age_seconds > 0 ? static_cast<double>(deaths) * 3600.0 / static_cast<double>(age_seconds) : 0.0;
    };

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%zu characters | %lldh played | %lld deaths | %.2f deaths/h",
                       characters.size(), total_age / 3600, total_deaths, deaths_per_hour(total_deaths, total_age));

    if (missing_inventory_scope)
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "Inventory slots need an API key with the \"inventories\" scope.");

    ImGui::Spacing();

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                                      ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    const auto table_width = min(ImGui::GetContentRegionAvail().x, 1000.0f);
    if (!ImGui::BeginTable("CharactersTable", 9, flags, ImVec2(table_width, 400.0f)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 150.0f, 0);
    ImGui::TableSetupColumn("Lvl", ImGuiTableColumnFlags_WidthFixed, 35.0f, 1);
    ImGui::TableSetupColumn("Profession", ImGuiTableColumnFlags_WidthFixed, 100.0f, 2);
    ImGui::TableSetupColumn("Race", ImGuiTableColumnFlags_WidthFixed, 70.0f, 3);
    ImGui::TableSetupColumn("Played", ImGuiTableColumnFlags_WidthFixed, 90.0f, 4);
    ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_WidthFixed, 90.0f, 5);
    ImGui::TableSetupColumn("Deaths", ImGuiTableColumnFlags_WidthFixed, 60.0f, 6);
    ImGui::TableSetupColumn("Deaths/h", ImGuiTableColumnFlags_WidthFixed, 70.0f, 7);
    ImGui::TableSetupColumn("Slots", ImGuiTableColumnFlags_WidthFixed, 50.0f, 8);
    ImGui::TableHeadersRow();

    if (auto *sort_specs = ImGui::TableGetSortSpecs();
        sort_specs != nullptr && sort_specs->SpecsDirty && sort_specs->SpecsCount > 0)
    {
        const auto &spec = sort_specs->Specs[0];
        const auto compare = [&](const CharacterInfo &lhs, const CharacterInfo &rhs)
        {
            switch (spec.ColumnUserID)
            {
            case 1:
                return lhs.level < rhs.level;
            case 2:
                return lhs.profession < rhs.profession;
            case 3:
                return lhs.race < rhs.race;
            case 4:
                return lhs.age_seconds < rhs.age_seconds;
            case 5:
                return lhs.created < rhs.created;
            case 6:
                return lhs.deaths < rhs.deaths;
            case 7:
                return deaths_per_hour(lhs.deaths, lhs.age_seconds) < deaths_per_hour(rhs.deaths, rhs.age_seconds);
            case 8:
                return lhs.inventory_slots < rhs.inventory_slots;
            default:
                return lhs.name < rhs.name;
            }
        };

        const auto ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
        std::sort(characters.begin(), characters.end(),
                  [&](const CharacterInfo &lhs, const CharacterInfo &rhs)
                  { return ascending ? compare(lhs, rhs) : compare(rhs, lhs); });
        sort_specs->SpecsDirty = false;
    }

    for (const auto &info : characters)
    {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(info.name.c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%d", info.level);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(info.profession.c_str());

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(info.race.c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%lldh %lldm", info.age_seconds / 3600, (info.age_seconds % 3600) / 60);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(info.created.substr(0, 10).c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%d", info.deaths);

        ImGui::TableNextColumn();
        if (info.age_seconds > 0)
            ImGui::Text("%.2f", deaths_per_hour(info.deaths, info.age_seconds));
        else
            ImGui::TextDisabled("-");

        ImGui::TableNextColumn();
        if (info.has_inventory)
            ImGui::Text("%d", info.inventory_slots);
        else
            ImGui::TextDisabled("-");
    }

    ImGui::EndTable();

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Crafting");
    ImGui::Separator();

    for (const auto &info : characters)
    {
        if (info.crafting.empty())
            continue;

        ImGui::TextUnformatted(info.name.c_str());
        ImGui::SameLine(180.0f);
        for (auto craft_index = 0; craft_index < static_cast<int>(info.crafting.size()); ++craft_index)
        {
            const auto &craft = info.crafting[craft_index];
            if (craft_index > 0)
                ImGui::SameLine();
            /* inactive disciplines are still learned but cost gold to swap back in */
            if (craft.active)
                ImGui::Text("%s %d", craft.discipline.c_str(), craft.rating);
            else
                ImGui::TextDisabled("%s %d", craft.discipline.c_str(), craft.rating);
        }
    }
}

void Render::render_note_input(const std::string &account_name)
{
    auto &buffer = note_buffers[account_name];
    if (buffer.empty())
    {
        buffer.assign(NOTE_BUFFER_SIZE, '\0');
        strncpy_s(buffer.data(), buffer.size(), SquadNotes::GetNote(account_name).c_str(), _TRUNCATE);
    }

    ImGui::PushID(account_name.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##Note", "Add a note...", buffer.data(), buffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit())
        SquadNotes::SetNote(account_name, buffer.data());
    ImGui::PopID();
}

void Render::squad_notes_child()
{
    const auto *rtapi = Globals::RTAPIData;
    const auto rtapi_available = rtapi != nullptr && rtapi->GameBuild != 0;

    if (!rtapi_available)
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "RTAPI is not loaded - squad members can only be tracked while it is running.");

    const auto squad = SquadNotes::CurrentSquad();
    const char *group_label = "Group";
    if (rtapi_available)
    {
        switch (rtapi->GroupType)
        {
        case RTAPI::EGroupType::Party:
            group_label = "Party";
            break;
        case RTAPI::EGroupType::RaidSquad:
        case RTAPI::EGroupType::Squad:
            group_label = "Squad";
            break;
        default:
            group_label = "Solo";
            break;
        }
    }

    const auto reported_count = rtapi_available ? static_cast<int>(rtapi->GroupMemberCount) : 0;
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s | %zu tracked / %d reported members",
                       group_label, squad.size(), reported_count);

    /* RTAPI only announces members that join after the addon subscribed, so members who were
       already grouped up before that never show up until they rejoin. */
    if (rtapi_available && reported_count > static_cast<int>(squad.size()))
        ImGui::TextDisabled("Members who joined before GW2HB loaded are not tracked until they rejoin.");

    ImGui::Spacing();

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (squad.empty())
    {
        ImGui::TextDisabled("Nobody in the squad right now.");
    }
    else if (ImGui::BeginTable("SquadNotesTable", 4, flags, ImVec2(0.0f, 260.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Sub", ImGuiTableColumnFlags_WidthFixed, 35.0f);
        ImGui::TableSetupColumn("Account", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto &member : squad)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (member.subgroup > 0)
                ImGui::Text("%d", member.subgroup);
            else
                ImGui::TextUnformatted("-");

            ImGui::TableNextColumn();
            if (member.is_commander)
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", member.account_name.c_str());
            else if (member.is_self)
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", member.account_name.c_str());
            else
                ImGui::TextUnformatted(member.account_name.c_str());
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                ImGui::SetClipboardText(member.account_name.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(member.character_name.c_str());

            ImGui::TableNextColumn();
            render_note_input(member.account_name);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Show all saved accounts", &show_all_account_notes);
    if (!show_all_account_notes)
        return;

    static char account_filter[64] = {};
    static char new_account[64] = {};

    ImGui::TextUnformatted("Search");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##AccountNoteFilter", account_filter, sizeof(account_filter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##NewAccountNote", "Account.1234", new_account, sizeof(new_account));
    ImGui::SameLine();
    if (ImGui::Button("Add##AccountNote") && new_account[0] != '\0')
    {
        SquadNotes::AddAccount(new_account);
        new_account[0] = '\0';
    }

    const auto to_lower = [](std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    const auto filter = to_lower(account_filter);

    const auto all_notes = SquadNotes::AllNotes();
    std::string account_to_remove;

    if (ImGui::BeginTable("AllAccountNotesTable", 4, flags, ImVec2(0.0f, 260.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Account", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Last Seen", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const auto &[account_name, note] : all_notes)
        {
            if (!filter.empty() && to_lower(account_name).find(filter) == std::string::npos)
                continue;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(account_name.c_str());
            if (ImGui::IsItemHovered() && !note.last_character.empty())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Last character: %s\nSeen %d time(s)", note.last_character.c_str(), note.times_seen);
                ImGui::EndTooltip();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(note.last_seen.c_str());

            ImGui::TableNextColumn();
            render_note_input(account_name);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton(("Remove##" + account_name).c_str()))
                account_to_remove = account_name;
        }

        ImGui::EndTable();
    }

    if (!account_to_remove.empty())
    {
        SquadNotes::RemoveAccount(account_to_remove);
        note_buffers.erase(account_to_remove);
    }
}

void Render::collections_child()
{
    if (!account_skins_requested)
    {
        if (Settings::APIKey.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Set an API key in the Settings tab to check collections.");
            return;
        }
        const auto url = L"https://api.guildwars2.com/v2/account/skins?access_token=" +
                         std::wstring(Settings::APIKey.begin(), Settings::APIKey.end());
        account_skins_future = HTTPClient::GetRequestAsync(url);
        account_skins_requested = true;
        account_skins_loaded = false;
        account_skins_error.clear();
    }

    if (account_skins_future.has_value() &&
        account_skins_future->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            const auto j = json::parse(account_skins_future->get());
            if (j.is_array())
            {
                unlocked_skins.clear();
                for (const auto &id : j)
                    unlocked_skins.insert(id.get<int>());

                /* only evaluate collections not already known finished, then persist the result */
                auto newly_finished = false;
                for (const auto &[name, skin_ids] : CollectionSkins::COLLECTIONS)
                {
                    if (Settings::FinishedCollections.count(name) > 0)
                        continue;
                    const auto complete = !skin_ids.empty() &&
                                          std::all_of(skin_ids.begin(), skin_ids.end(),
                                                      [this](const auto &skin)
                                                      { return unlocked_skins.count(skin.first) > 0; });
                    if (complete)
                    {
                        Settings::FinishedCollections.insert(name);
                        newly_finished = true;
                    }
                }
                if (newly_finished)
                    Settings::Save(Globals::SettingsPath);
            }
            else if (j.contains("text"))
                account_skins_error = j["text"].get<std::string>();
            else
                account_skins_error = "Unexpected response from GW2 API.";
        }
        catch (const std::exception &e)
        {
            account_skins_error = std::string("Failed to parse response: ") + e.what();
        }
        account_skins_future.reset();
        account_skins_loaded = true;
    }

    /* Fetch trading-post prices for the buyable items behind every still-locked skin. */
    if (account_skins_loaded && account_skins_error.empty() && !collection_prices_requested)
    {
        collection_item_prices.clear();
        collection_price_futures.clear();

        std::set<int> needed_items;
        for (const auto &[name, skin_ids] : CollectionSkins::COLLECTIONS)
        {
            if (Settings::FinishedCollections.count(name) > 0)
                continue;
            for (const auto &[skin_id, skin_name] : skin_ids)
            {
                if (unlocked_skins.count(skin_id) > 0)
                    continue;
                const auto item_it = CollectionSkins::SKIN_TO_ITEM.find(skin_id);
                if (item_it != CollectionSkins::SKIN_TO_ITEM.end())
                    needed_items.insert(item_it->second);
            }
        }

        const std::vector<int> ids(needed_items.begin(), needed_items.end());
        constexpr size_t batch_size = 200;
        for (size_t start = 0; start < ids.size(); start += batch_size)
        {
            std::string id_param;
            for (size_t i = start; i < ids.size() && i < start + batch_size; ++i)
            {
                if (!id_param.empty())
                    id_param += ",";
                id_param += std::to_string(ids[i]);
            }
            const auto url = L"https://api.guildwars2.com/v2/commerce/prices?ids=" +
                             std::wstring(id_param.begin(), id_param.end());
            collection_price_futures.push_back(HTTPClient::GetRequestAsync(url));
        }

        collection_prices_requested = true;
        collection_prices_loaded = collection_price_futures.empty();
    }

    if (collection_prices_requested && !collection_prices_loaded)
    {
        const auto all_ready = std::all_of(collection_price_futures.begin(), collection_price_futures.end(),
                                           [](std::future<std::string> &f)
                                           { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
        if (all_ready)
        {
            for (auto &f : collection_price_futures)
            {
                try
                {
                    const auto j = json::parse(f.get());
                    if (!j.is_array())
                        continue;
                    for (const auto &entry : j)
                    {
                        const auto id = entry.value("id", 0);
                        const auto buy = entry.contains("buys") && !entry["buys"].is_null()
                                             ? entry["buys"].value("unit_price", 0)
                                             : 0;
                        const auto sell = entry.contains("sells") && !entry["sells"].is_null()
                                              ? entry["sells"].value("unit_price", 0)
                                              : 0;
                        collection_item_prices[id] = {buy, sell};
                    }
                }
                catch (const std::exception &)
                {
                    /* skip a malformed price batch; remaining items just show as unavailable */
                }
            }
            collection_price_futures.clear();
            collection_prices_loaded = true;
        }
    }

    if (ImGui::Button("Check for newly finished"))
    {
        account_skins_requested = false;
        collection_prices_requested = false;
        collection_prices_loaded = false;
    }

    ImGui::SameLine();
    if (!account_skins_loaded)
        ImGui::TextUnformatted("Loading...");
    else if (!account_skins_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", account_skins_error.c_str());

    if (CollectionSkins::COLLECTIONS.empty())
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("No collection data. Run scripts/scrape_bl_collections.py to populate it.");
        return;
    }

    std::vector<const decltype(CollectionSkins::COLLECTIONS)::value_type *> done;
    std::vector<const decltype(CollectionSkins::COLLECTIONS)::value_type *> not_done;
    for (const auto &collection : CollectionSkins::COLLECTIONS)
    {
        const auto finished = Settings::FinishedCollections.count(collection.first) > 0;
        (finished ? done : not_done).push_back(&collection);
    }

    std::vector<LockedCollection> priced;
    std::vector<LockedCollection> no_price;
    for (const auto *collection : not_done)
    {
        LockedCollection entry{collection};
        for (const auto &[skin_id, skin_name] : collection->second)
        {
            if (unlocked_skins.count(skin_id) > 0)
                continue;

            NeededRow row{skin_name, 0, 0, false};
            const auto item_it = CollectionSkins::SKIN_TO_ITEM.find(skin_id);
            if (item_it != CollectionSkins::SKIN_TO_ITEM.end())
            {
                const auto price_it = collection_item_prices.find(item_it->second);
                if (price_it != collection_item_prices.end())
                {
                    row.buy_order = price_it->second.first;
                    row.instant_buy = price_it->second.second;
                    row.has_price = true;
                    entry.total_buy_order += row.buy_order;
                    entry.total_instant_buy += row.instant_buy;
                }
            }
            entry.rows.push_back(std::move(row));
        }
        entry.has_any_price = entry.total_instant_buy > 0 || entry.total_buy_order > 0;
        (entry.has_any_price ? priced : no_price).push_back(std::move(entry));
    }

    const auto sort_value = [this](const LockedCollection &entry) -> long long
    {
        switch (collection_sort)
        {
        case CollectionSort::BuyOrder:
            return entry.total_buy_order;
        case CollectionSort::ItemsNeeded:
            return static_cast<long long>(entry.rows.size());
        case CollectionSort::InstantBuy:
        default:
            return entry.total_instant_buy;
        }
    };
    const auto sort_collections = [&](auto &collections)
    {
        std::stable_sort(collections.begin(), collections.end(),
                         [&](const LockedCollection &lhs, const LockedCollection &rhs)
                         {
                             return collections_sort_ascending
                                        ? sort_value(lhs) < sort_value(rhs)
                                        : sort_value(lhs) > sort_value(rhs);
                         });
    };
    sort_collections(priced);
    sort_collections(no_price);

    const auto coins_str = [](long long copper)
    {
        const auto p = copper_to_price(static_cast<int>(copper));
        return std::to_string(p.gold) + "g " + std::to_string(p.silver) + "s " +
               std::to_string(p.copper) + "c";
    };

    const auto render_locked_collection = [&](const LockedCollection &entry)
    {
        const auto *collection = entry.collection;
        const auto header = collection->first + "  (" + std::to_string(entry.rows.size()) + ")  Order: " +
                            coins_str(entry.total_buy_order) + " | Insta: " + coins_str(entry.total_instant_buy) +
                            "##" + collection->first;
        if (!ImGui::CollapsingHeader(header.c_str()))
            return;

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable(("NeededItems##" + collection->first).c_str(), 3, flags))
        {
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Buy Order", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Instant Buy", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();

            for (const auto &row : entry.rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.name.c_str());
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    ImGui::SetClipboardText(row.name.c_str());
                    copied_name_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    copied_name_popup_requested = true;
                }

                ImGui::TableNextColumn();
                if (row.has_price)
                {
                    const auto p = copper_to_price(row.buy_order);
                    ImGui::Text("%dg %ds %dc", p.gold, p.silver, p.copper);
                }
                else
                    ImGui::TextUnformatted("-");

                ImGui::TableNextColumn();
                if (row.has_price)
                {
                    const auto p = copper_to_price(row.instant_buy);
                    ImGui::Text("%dg %ds %dc", p.gold, p.silver, p.copper);
                }
                else
                    ImGui::TextUnformatted("-");
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Total");
            ImGui::TableNextColumn();
            {
                const auto p = copper_to_price(static_cast<int>(entry.total_buy_order));
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%dg %ds %dc", p.gold, p.silver, p.copper);
            }
            ImGui::TableNextColumn();
            {
                const auto p = copper_to_price(static_cast<int>(entry.total_instant_buy));
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%dg %ds %dc", p.gold, p.silver, p.copper);
            }

            ImGui::EndTable();
        }
    };

    long long grand_total_buy_order = 0;
    long long grand_total_instant_buy = 0;
    for (const auto &entry : priced)
    {
        grand_total_buy_order += entry.total_buy_order;
        grand_total_instant_buy += entry.total_instant_buy;
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Locked (%zu)", priced.size());
    ImGui::SameLine();
    ImGui::TextUnformatted("Sort:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    constexpr std::array sort_options = {
        std::pair{CollectionSort::InstantBuy, "Insta buy"},
        std::pair{CollectionSort::BuyOrder, "Order"},
        std::pair{CollectionSort::ItemsNeeded, "Items needed"},
    };
    const auto selected_sort = std::find_if(sort_options.begin(), sort_options.end(),
                                            [this](const auto &option)
                                            { return option.first == collection_sort; });
    const auto *sort_preview = selected_sort != sort_options.end() ? selected_sort->second : "Insta buy";
    if (ImGui::BeginCombo("##CollectionsSort", sort_preview))
    {
        for (const auto &[sort, label] : sort_options)
        {
            const auto selected = collection_sort == sort;
            if (ImGui::Selectable(label, selected))
                collection_sort = sort;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    const char *direction_labels[] = {"Ascending", "Descending"};
    auto direction_index = collections_sort_ascending ? 0 : 1;
    if (ImGui::Combo("##CollectionsSortDirection", &direction_index, direction_labels, IM_ARRAYSIZE(direction_labels)))
        collections_sort_ascending = direction_index == 0;

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "All open collections  Order: %s | Insta: %s",
                       coins_str(grand_total_buy_order).c_str(), coins_str(grand_total_instant_buy).c_str());
    ImGui::Separator();

    for (const auto &entry : priced)
        render_locked_collection(entry);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Locked - No Price Data (%zu)", no_price.size());
    ImGui::Separator();

    for (const auto &entry : no_price)
        render_locked_collection(entry);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::CollapsingHeader((std::string("Unlocked (") + std::to_string(done.size()) + ")").c_str()))
    {
        for (const auto *collection : done)
            ImGui::BulletText("%s", collection->first.c_str());
    }

    if (copied_name_popup_requested)
    {
        ImGui::OpenPopup("NameCopiedPopup");
        copied_name_popup_requested = false;
    }

    if (ImGui::BeginPopup("NameCopiedPopup", ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Name copied");
        if (std::chrono::steady_clock::now() >= copied_name_until)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Render::delivery_child()
{
    if (!delivery_requested)
    {
        if (Settings::APIKey.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Set an API key in the Settings tab to check the delivery box.");
            return;
        }
        const auto url = L"https://api.guildwars2.com/v2/commerce/delivery?access_token=" +
                         std::wstring(Settings::APIKey.begin(), Settings::APIKey.end());
        delivery_future = HTTPClient::GetRequestAsync(url);
        delivery_requested = true;
        delivery_loaded = false;
        delivery_error.clear();
        delivery_names_requested = false;
        delivery_names_loaded = false;
    }

    if (delivery_future.has_value() &&
        delivery_future->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            const auto j = json::parse(delivery_future->get());
            if (j.is_object() && j.contains("items"))
            {
                delivery_coins = j.value("coins", 0LL);
                delivery_items.clear();
                for (const auto &item : j["items"])
                    delivery_items.emplace_back(item.value("id", 0), item.value("count", 0));
            }
            else if (j.contains("text"))
                delivery_error = j["text"].get<std::string>();
            else
                delivery_error = "Unexpected response from GW2 API.";
        }
        catch (const std::exception &e)
        {
            delivery_error = std::string("Failed to parse response: ") + e.what();
        }
        delivery_future.reset();
        delivery_loaded = true;
    }

    /* resolve item names for everything currently sitting in the delivery box */
    if (delivery_loaded && delivery_error.empty() && !delivery_names_requested)
    {
        delivery_name_futures.clear();

        std::vector<int> ids;
        for (const auto &[id, count] : delivery_items)
            if (delivery_item_names.find(id) == delivery_item_names.end())
                ids.push_back(id);

        constexpr size_t batch_size = 200;
        for (size_t start = 0; start < ids.size(); start += batch_size)
        {
            std::string id_param;
            for (size_t i = start; i < ids.size() && i < start + batch_size; ++i)
            {
                if (!id_param.empty())
                    id_param += ",";
                id_param += std::to_string(ids[i]);
            }
            const auto url = L"https://api.guildwars2.com/v2/items?ids=" +
                             std::wstring(id_param.begin(), id_param.end());
            delivery_name_futures.push_back(HTTPClient::GetRequestAsync(url));
        }

        delivery_names_requested = true;
        delivery_names_loaded = delivery_name_futures.empty();
    }

    if (delivery_names_requested && !delivery_names_loaded)
    {
        const auto all_ready = std::all_of(delivery_name_futures.begin(), delivery_name_futures.end(),
                                           [](std::future<std::string> &f)
                                           { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
        if (all_ready)
        {
            for (auto &f : delivery_name_futures)
            {
                try
                {
                    const auto j = json::parse(f.get());
                    if (!j.is_array())
                        continue;
                    for (const auto &entry : j)
                        delivery_item_names[entry.value("id", 0)] = entry.value("name", std::string{});
                }
                catch (const std::exception &)
                {
                    /* skip a malformed batch; those items just fall back to their id */
                }
            }
            delivery_name_futures.clear();
            delivery_names_loaded = true;
        }
    }

    if (ImGui::Button("Refresh Delivery Box"))
    {
        delivery_requested = false;
        delivery_names_requested = false;
        delivery_names_loaded = false;
    }

    ImGui::SameLine();
    if (!delivery_loaded)
        ImGui::TextUnformatted("Loading...");
    else if (!delivery_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", delivery_error.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!delivery_loaded || !delivery_error.empty())
        return;

    const auto coins = copper_to_price(static_cast<int>(delivery_coins));
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Coins to collect: %dg %ds %dc",
                       coins.gold, coins.silver, coins.copper);

    ImGui::Spacing();

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
    if (!ImGui::BeginTable("DeliveryItemsTable", 2, flags))
        return;

    ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();

    if (delivery_items.empty())
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("No items to pick up.");
    }

    for (const auto &[id, count] : delivery_items)
    {
        const auto name_it = delivery_item_names.find(id);
        const auto name = name_it != delivery_item_names.end() && !name_it->second.empty()
                              ? name_it->second
                              : std::string("Item #") + std::to_string(id);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", count);
    }

    ImGui::EndTable();
}

void Render::watchlist_child()
{
    static char item_id_input[16] = {};
    static char item_name_input[64] = {};

    ImGui::TextUnformatted("Add Item ID");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    const auto id_submitted = ImGui::InputText("##WatchlistItemId", item_id_input, sizeof(item_id_input), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    const auto name_submitted = ImGui::InputText("##WatchlistItemName", item_name_input, sizeof(item_name_input), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add##Watchlist") || id_submitted || name_submitted)
    {
        const auto item_id = std::atoi(item_id_input);
        auto &custom_items = Settings::CustomItems;
        const auto already_tracked = std::find_if(custom_items.begin(), custom_items.end(), [item_id](const auto &custom_item)
                                                  { return custom_item.item_id == item_id; });
        if (item_id > 0 && already_tracked == custom_items.end())
        {
            custom_items.push_back(CustomItem{item_id, item_name_input});
            Settings::WatchlistPriceAlerts.push_back(WatchlistPriceAlert{.item_id = item_id});
            Settings::Save(Globals::SettingsPath);
            data.requested = false;
        }
        item_id_input[0] = '\0';
        item_name_input[0] = '\0';
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (Settings::CustomItems.empty())
    {
        ImGui::TextUnformatted("No custom item IDs added yet. GW2 TP prices (buy/sell/flip) will show here.");
        return;
    }

    if (data.loaded)
    {
        if (ImGui::Button("Refresh Prices##Watchlist"))
        {
            data.loaded = false;
            data.requested = false;
            data.api_data.clear();
            data.futures.clear();
            data.requesting();
        }
    }
    else
    {
        ImGui::TextUnformatted("Loading...");
    }

    ImGui::Spacing();

    const auto window_width = ImGui::GetWindowContentRegionWidth();
    const auto large_window = window_width > 450.0F;
    const auto very_large_window = window_width > 750.0F;
    const auto child_size = ImVec2(window_width * (very_large_window ? 0.33f : (large_window ? 0.5f : 1.0F)), TABLE_HEIGHT_PX + 90.0f);

    auto settings_changed = false;
    for (const auto &custom_item : Settings::CustomItems)
    {
        const auto alert = std::find_if(Settings::WatchlistPriceAlerts.begin(), Settings::WatchlistPriceAlerts.end(), [&](const auto &candidate)
                                        { return candidate.item_id == custom_item.item_id; });
        if (alert == Settings::WatchlistPriceAlerts.end())
        {
            Settings::WatchlistPriceAlerts.push_back(WatchlistPriceAlert{.item_id = custom_item.item_id});
            settings_changed = true;
        }
    }

    auto remove_id = -1;
    auto idx = 0U;
    for (const auto &custom_item : Settings::CustomItems)
    {
        const auto item_id = custom_item.item_id;
        const auto display_name = custom_item.name.empty() ? std::to_string(item_id) : custom_item.name;
        ImGui::BeginChild(("watchlistChild" + std::to_string(idx)).c_str(), child_size, false, ImGuiWindowFlags_AlwaysAutoResize);

        if (ImGui::SmallButton(("Remove##" + std::to_string(item_id)).c_str()))
            remove_id = item_id;

        auto &alert = *std::find_if(Settings::WatchlistPriceAlerts.begin(), Settings::WatchlistPriceAlerts.end(), [item_id](const auto &candidate)
                                    { return candidate.item_id == item_id; });
        ImGui::SameLine();
        ImGui::TextDisabled("Price alerts (0 disables)");
        if (render_price_threshold_input("Buy >=", "BuyAlert", alert.buy_threshold))
        {
            alert.buy_crossed = false;
            watchlist_alerts_evaluated_for_load = false;
            settings_changed = true;
        }
        if (render_price_threshold_input("Sell <=", "SellAlert", alert.sell_threshold))
        {
            alert.sell_crossed = false;
            watchlist_alerts_evaluated_for_load = false;
            settings_changed = true;
        }

        render_custom_item_table("custom_" + std::to_string(item_id), display_name);

        ImGui::EndChild();
        if (very_large_window && (idx % 3 != 2))
            ImGui::SameLine();
        else if (!very_large_window && large_window && (idx % 2 == 0))
            ImGui::SameLine();
        ++idx;
    }

    if (remove_id != -1)
    {
        auto &custom_items = Settings::CustomItems;
        custom_items.erase(std::remove_if(custom_items.begin(), custom_items.end(), [remove_id](const auto &custom_item)
                                          { return custom_item.item_id == remove_id; }),
                           custom_items.end());
        Settings::WatchlistPriceAlerts.erase(
            std::remove_if(Settings::WatchlistPriceAlerts.begin(), Settings::WatchlistPriceAlerts.end(), [remove_id](const auto &alert)
                           { return alert.item_id == remove_id; }),
            Settings::WatchlistPriceAlerts.end());
        watchlist_price_alert_notifications.erase(
            std::remove_if(watchlist_price_alert_notifications.begin(), watchlist_price_alert_notifications.end(), [remove_id](const auto &notification)
                           { return notification.item_id == remove_id; }),
            watchlist_price_alert_notifications.end());
        pending_watchlist_price_alert_notifications.erase(
            std::remove_if(pending_watchlist_price_alert_notifications.begin(), pending_watchlist_price_alert_notifications.end(), [remove_id](const auto &notification)
                           { return notification.item_id == remove_id; }),
            pending_watchlist_price_alert_notifications.end());
        data.api_data.erase("custom_" + std::to_string(remove_id));
        settings_changed = true;
    }

    if (settings_changed)
        Settings::Save(Globals::SettingsPath);
}

void Render::update_watchlist_price_alerts()
{
    if (!data.loaded)
    {
        watchlist_alerts_evaluated_for_load = false;
        return;
    }

    if (watchlist_alerts_evaluated_for_load)
        return;

    watchlist_alerts_evaluated_for_load = true;
    const auto now = std::chrono::steady_clock::now();
    auto settings_changed = false;

    for (auto &alert : Settings::WatchlistPriceAlerts)
    {
        const auto custom_item = std::find_if(Settings::CustomItems.begin(), Settings::CustomItems.end(), [&](const auto &item)
                                              { return item.item_id == alert.item_id; });
        const auto prices = data.api_data.find("custom_" + std::to_string(alert.item_id));
        if (custom_item == Settings::CustomItems.end() || prices == data.api_data.end())
            continue;

        const auto item_name = custom_item->name.empty() ? "Item #" + std::to_string(alert.item_id) : custom_item->name;
        const auto evaluate = [&](const std::optional<int> current_price, const int threshold, bool &was_crossed, const bool is_buy_alert)
        {
            if (!current_price.has_value() || *current_price <= 0)
                return;

            const auto crossed = threshold > 0 && (is_buy_alert ? *current_price >= threshold : *current_price <= threshold);
            if (crossed && !was_crossed)
            {
                pending_watchlist_price_alert_notifications.push_back(PriceAlertNotification{
                    .item_id = alert.item_id,
                    .item_name = item_name,
                    .current_price = *current_price,
                    .threshold = threshold,
                    .is_buy_alert = is_buy_alert,
                    .created_at = now,
                });
            }

            if (was_crossed != crossed)
            {
                was_crossed = crossed;
                settings_changed = true;
            }
        };

        evaluate(get_api_int_value(prices->second, "buy"), alert.buy_threshold, alert.buy_crossed, true);
        evaluate(get_api_int_value(prices->second, "sell"), alert.sell_threshold, alert.sell_crossed, false);
    }

    if (settings_changed)
        Settings::Save(Globals::SettingsPath);
}

void Render::update_outdated_order_notifications()
{
    if (!Settings::ShowOutdatedOrders)
        return;

    static constexpr auto POLL_INTERVAL = std::chrono::seconds(60);

    const auto now = std::chrono::steady_clock::now();

    if (!my_orders_polling_started || (now - last_my_orders_poll_time) >= POLL_INTERVAL)
    {
        /* force a fresh poll, but only every POLL_INTERVAL so we don't clobber the tab's request each frame */
        data.my_orders_requested = false;
        data.request_my_orders();
        last_my_orders_poll_time = now;
        my_orders_polling_started = true;
    }

    data.store_my_orders();

    if (!data.my_orders_loaded)
        return;

    std::set<int> current_ids;
    for (const auto &order : data.my_orders_buys)
        current_ids.insert(order.item_id);
    for (const auto &order : data.my_orders_sells)
        current_ids.insert(order.item_id);

    /* an order is no longer outdated - allow it to notify again if it becomes outdated later */
    auto settings_changed = false;
    for (auto it = Settings::NotifiedOutdatedOrderIds.begin(); it != Settings::NotifiedOutdatedOrderIds.end();)
    {
        if (current_ids.find(*it) == current_ids.end())
        {
            it = Settings::NotifiedOutdatedOrderIds.erase(it);
            settings_changed = true;
        }
        else
            ++it;
    }

    if (!outdated_order_baseline_initialized)
    {
        /* don't spam notifications for orders that were already outdated when the addon started */
        Settings::NotifiedOutdatedOrderIds = current_ids;
        outdated_order_baseline_initialized = true;
        Settings::Save(Globals::SettingsPath);
        return;
    }

    const auto add_new_notifications = [&](const std::vector<MyOrderEntry> &orders)
    {
        for (const auto &order : orders)
        {
            /* persisted so a restart won't re-trigger a popup already shown for this order */
            if (Settings::NotifiedOutdatedOrderIds.insert(order.item_id).second)
            {
                pending_outdated_order_notifications.push_back(OutdatedOrderNotification{
                    .item_id = order.item_id,
                    .item_name = order.item_name,
                    .my_price = order.my_price,
                    .curr_price = order.curr_price,
                    .created_at = now,
                });
                settings_changed = true;
            }
        }
    };

    add_new_notifications(data.my_orders_buys);
    add_new_notifications(data.my_orders_sells);

    if (settings_changed)
        Settings::Save(Globals::SettingsPath);
}

Texture *Render::get_item_icon_texture(int item_id)
{
    auto &state = item_icon_states[item_id];

    if (state.identifier.empty())
        state.identifier = "GW2HB_ITEM_ICON_" + std::to_string(item_id);

    const auto cache_path = Globals::AddonPath / "item_icons" / (std::to_string(item_id) + ".png");

    if (!state.icon_url_requested)
    {
        if (std::filesystem::exists(cache_path))
        {
            /* reuse the icon already downloaded on a previous session instead of hitting the network again */
            Globals::APIDefs->LoadTextureFromFile(state.identifier.c_str(), cache_path.string().c_str(), nullptr);
            state.icon_url_requested = true;
            state.image_requested = true;
            state.texture_load_requested = true;
        }
        else
        {
            const auto wstr_url = L"https://api.guildwars2.com/v2/items/" + std::to_wstring(item_id);
            state.icon_info_future = HTTPClient::GetRequestAsync(wstr_url);
            state.icon_url_requested = true;
        }
    }
    else if (state.icon_info_future.has_value())
    {
        if (state.icon_info_future->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                const auto response = state.icon_info_future->get();
                const auto j = json::parse(response);
                const auto icon_url = j.value("icon", std::string{});
                if (!icon_url.empty())
                    state.image_future = HTTPClient::GetRequestAsync(std::wstring(icon_url.begin(), icon_url.end()));
            }
            catch (const std::exception &)
            {
                /* no icon available for this item; keep showing the popup without an image */
            }
            state.icon_info_future.reset();
            state.image_requested = true;
        }
    }
    else if (state.image_requested && state.image_future.has_value())
    {
        if (state.image_future->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const auto image_bytes = state.image_future->get();
            if (!image_bytes.empty())
            {
                try
                {
                    std::filesystem::create_directories(cache_path.parent_path());
                    std::ofstream file(cache_path, std::ios::binary);
                    file.write(image_bytes.data(), static_cast<std::streamsize>(image_bytes.size()));
                }
                catch (const std::filesystem::filesystem_error &)
                {
                    /* caching is best-effort; texture can still be loaded from memory below */
                }

                Globals::APIDefs->LoadTextureFromMemory(state.identifier.c_str(), (void *)image_bytes.data(), image_bytes.size(), nullptr);
            }

            state.image_future.reset();
            state.texture_load_requested = true;
        }
    }

    return Globals::APIDefs->GetTexture(state.identifier.c_str());
}

void Render::render_outdated_order_notifications()
{
    if (!Settings::ShowOutdatedOrders)
        return;

    const auto in_combat = Settings::HideNotificationsInCombat && Globals::IsInCombat();

    if (!in_combat && !pending_outdated_order_notifications.empty())
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto &pending : pending_outdated_order_notifications)
        {
            pending.created_at = now;
            outdated_order_notifications.push_back(std::move(pending));
        }
        pending_outdated_order_notifications.clear();
    }

    if (outdated_order_notifications.empty() || in_combat)
        return;

    static constexpr auto NOTIFICATION_DURATION = std::chrono::seconds(10);
    static constexpr auto NOTIFICATION_WIDTH = 260.0f;
    static constexpr auto NOTIFICATION_SPACING = 8.0f;

    const auto now = std::chrono::steady_clock::now();
    auto offset_y = 10.0f;

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_AlwaysAutoResize;

    auto it = outdated_order_notifications.begin();
    while (it != outdated_order_notifications.end())
    {
        if ((now - it->created_at) >= NOTIFICATION_DURATION)
        {
            it = outdated_order_notifications.erase(it);
            continue;
        }

        const auto my_price = copper_to_price(it->my_price);
        const auto curr_price = copper_to_price(it->curr_price);
        const auto window_id = "##OutdatedOrder" + std::to_string(it->item_id);

        ImGui::SetNextWindowPos(ImVec2(10.0f, offset_y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(NOTIFICATION_WIDTH, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.9f);

        auto dismissed = false;

        if (ImGui::Begin(window_id.c_str(), nullptr, flags))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Outdated Order");
            ImGui::Separator();

            const auto *icon_texture = get_item_icon_texture(it->item_id);
            if (icon_texture != nullptr && icon_texture->Resource != nullptr)
            {
                ImGui::Image((ImTextureID)icon_texture->Resource, ImVec2(32.0f, 32.0f));
                ImGui::SameLine();
            }

            ImGui::BeginGroup();
            ImGui::TextUnformatted(it->item_name.c_str());
            ImGui::Text("My Price: %dg %ds %dc", my_price.gold, my_price.silver, my_price.copper);
            ImGui::Text("Curr Price: %dg %ds %dc", curr_price.gold, curr_price.silver, curr_price.copper);
            ImGui::EndGroup();
            ImGui::TextDisabled("(click to dismiss)");

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                dismissed = true;
        }
        const auto window_height = ImGui::GetWindowSize().y;
        ImGui::End();

        if (dismissed)
        {
            it = outdated_order_notifications.erase(it);
            continue;
        }

        offset_y += window_height + NOTIFICATION_SPACING;
        ++it;
    }
}

void Render::render_watchlist_price_alert_notifications()
{
    const auto in_combat = Settings::HideNotificationsInCombat && Globals::IsInCombat();

    if (!in_combat && !pending_watchlist_price_alert_notifications.empty())
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto &pending : pending_watchlist_price_alert_notifications)
        {
            pending.created_at = now;
            watchlist_price_alert_notifications.push_back(std::move(pending));
        }
        pending_watchlist_price_alert_notifications.clear();
    }

    if (watchlist_price_alert_notifications.empty() || in_combat)
        return;

    static constexpr auto NOTIFICATION_DURATION = std::chrono::seconds(10);
    static constexpr auto NOTIFICATION_WIDTH = 280.0f;
    static constexpr auto NOTIFICATION_SPACING = 8.0f;

    const auto now = std::chrono::steady_clock::now();
    auto offset_y = 10.0f;

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_AlwaysAutoResize;

    auto it = watchlist_price_alert_notifications.begin();
    while (it != watchlist_price_alert_notifications.end())
    {
        if ((now - it->created_at) >= NOTIFICATION_DURATION)
        {
            it = watchlist_price_alert_notifications.erase(it);
            continue;
        }

        const auto current_price = copper_to_price(it->current_price);
        const auto threshold = copper_to_price(it->threshold);
        const auto window_id = "##PriceAlert" + std::to_string(it->item_id) + (it->is_buy_alert ? "Buy" : "Sell");
        const auto x = (std::max)(10.0f, ImGui::GetIO().DisplaySize.x - NOTIFICATION_WIDTH - 10.0f);

        ImGui::SetNextWindowPos(ImVec2(x, offset_y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(NOTIFICATION_WIDTH, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.9f);

        auto dismissed = false;
        if (ImGui::Begin(window_id.c_str(), nullptr, flags))
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s Price Alert", it->is_buy_alert ? "Buy" : "Sell");
            ImGui::Separator();

            const auto *icon_texture = get_item_icon_texture(it->item_id);
            if (icon_texture != nullptr && icon_texture->Resource != nullptr)
            {
                ImGui::Image((ImTextureID)icon_texture->Resource, ImVec2(32.0f, 32.0f));
                ImGui::SameLine();
            }

            ImGui::BeginGroup();
            ImGui::TextUnformatted(it->item_name.c_str());
            ImGui::Text("Current: %dg %ds %dc", current_price.gold, current_price.silver, current_price.copper);
            ImGui::Text("Threshold: %dg %ds %dc", threshold.gold, threshold.silver, threshold.copper);
            ImGui::EndGroup();
            ImGui::TextDisabled("(click to dismiss)");

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                dismissed = true;
        }
        const auto window_height = ImGui::GetWindowSize().y;
        ImGui::End();

        if (dismissed)
        {
            it = watchlist_price_alert_notifications.erase(it);
            continue;
        }

        offset_y += window_height + NOTIFICATION_SPACING;
        ++it;
    }
}

void Render::calculators_child()
{
    const auto window_width = ImGui::GetWindowContentRegionWidth();

    ImGui::BeginChild("CalculatorsContent", ImVec2(window_width, -1.0), false, ImGuiWindowFlags_AlwaysAutoResize);

    const auto available_width = ImGui::GetContentRegionAvail().x;
    const auto item_spacing = ImGui::GetStyle().ItemSpacing.x;
    const auto side_by_side = available_width >= 688.0f;
    const auto child_width = side_by_side ? (available_width - item_spacing) * 0.5f : available_width;

    ImGui::BeginChild("ProfitCalculator", ImVec2(child_width, 170.0f), false);
    RenderUI::render_profit_calculator();
    ImGui::EndChild();

    if (side_by_side)
        ImGui::SameLine();

    ImGui::BeginChild("GemExchangeCalculator", ImVec2(child_width, 170.0f), false);
    RenderUI::render_gem_gold_calculator();
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    RenderUI::render_krait_materials_calculator();

    ImGui::EndChild();
}

void Render::settings_child()
{
    static char api_key[256] = {};
    static bool initialized = false;
    if (!initialized)
    {
        strncpy_s(api_key, Settings::APIKey.c_str(), _TRUNCATE);
        initialized = true;
    }

    ImGui::TextUnformatted("API Key");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("##APIKey", api_key, sizeof(api_key));
    ImGui::SameLine();

    if (ImGui::Button("Save API Key"))
    {
        Settings::APIKey = api_key;
        Settings::Save(Globals::SettingsPath);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("Secondary API Keys (used only for the Completions tab)");

    static char secondary_label[64] = {};
    static char secondary_key[256] = {};
    ImGui::TextUnformatted("Label");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("##SecondaryLabel", secondary_label, sizeof(secondary_label));
    ImGui::SameLine();
    ImGui::TextUnformatted("Key");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("##SecondaryKey", secondary_key, sizeof(secondary_key));
    ImGui::SameLine();
    if (ImGui::Button("Add##SecondaryKey"))
    {
        if (secondary_key[0] != '\0')
        {
            const auto label = secondary_label[0] != '\0'
                                   ? std::string(secondary_label)
                                   : "Account " + std::to_string(Settings::SecondaryAPIKeys.size() + 1);
            Settings::SecondaryAPIKeys.push_back(SecondaryAccount{label, secondary_key});
            Settings::Save(Globals::SettingsPath);
            secondary_label[0] = '\0';
            secondary_key[0] = '\0';
        }
    }

    auto remove_secondary = -1;
    for (auto i = 0; i < static_cast<int>(Settings::SecondaryAPIKeys.size()); ++i)
    {
        const auto &account = Settings::SecondaryAPIKeys[i];
        ImGui::BulletText("%s", account.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(("Remove##Secondary" + std::to_string(i)).c_str()))
            remove_secondary = i;
    }

    if (remove_secondary != -1)
    {
        Settings::SecondaryAPIKeys.erase(Settings::SecondaryAPIKeys.begin() + remove_secondary);
        Settings::Save(Globals::SettingsPath);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Open Settings Folder"))
    {
        const auto settings_folder = Globals::SettingsPath.parent_path().string();
        ShellExecuteA(nullptr, "open", settings_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    ImGui::Spacing();
    if (ImGui::Button("Update Scripts"))
        ImGui::OpenPopup("Confirm Script Update");

    if (ImGui::BeginPopupModal("Confirm Script Update", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Download the latest scripts?");
        if (ImGui::Button("OK"))
        {
            RenderUI::update_scripts(Globals::AddonPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::Checkbox("Launch Scripts in Background", &Settings::ScriptsInBackground))
        Settings::Save(Globals::SettingsPath);

    if (ImGui::Checkbox("Show Outdated Orders Popup", &Settings::ShowOutdatedOrders))
        Settings::Save(Globals::SettingsPath);

    if (ImGui::Checkbox("Hide Notifications While In Combat", &Settings::HideNotificationsInCombat))
        Settings::Save(Globals::SettingsPath);

    ImGui::TextUnformatted("Ecto rate for salvaging");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputFloat("##EctoRate", &Settings::EctoRate, 0.01f, 1.0f, "%.2f"))
    {
        Settings::EctoRate = std::clamp(Settings::EctoRate, 0.0f, 1.0f);
        Settings::Save(Globals::SettingsPath);
    }
}

void Render::render()
{
    update_outdated_order_notifications();
    update_watchlist_price_alerts();
    render_outdated_order_notifications();
    render_watchlist_price_alert_notifications();

    if (!show_window)
        return;

    if (ImGui::Begin("GW2 HelperBox++", &show_window))
    {
        if (ImGui::BeginTabBar("GW2HBTabs"))
        {
            if (ImGui::BeginTabItem("Prices"))
            {
                top_section_child();
                table_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Delivery Box"))
            {
                delivery_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Orders"))
            {
                my_orders_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Watchlist"))
            {
                watchlist_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Calculators"))
            {
                calculators_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Collections"))
            {
                collections_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Completions"))
            {
                weekly_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Characters"))
            {
                characters_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Squad Notes"))
            {
                squad_notes_child();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                settings_child();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();
}
