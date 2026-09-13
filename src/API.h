
#pragma once

#include <array>
#include <map>
#include <set>
#include <string>

class API
{
public:
    static const inline std::wstring PRODUCTION_API_URL = L"https://gw2tp-production.up.railway.app/api";
    static const inline std::wstring LOCAL_API_URL = L"http://localhost:8000/api";

    static const inline std::set<std::string> OTHER_COMMANDS = {
        "t5_mats_buy",
        "mats_crafting_compare",
    };

    static const inline std::set<std::string> GEAR_COMMANDS = {
        "krait_shield_craft",
        "krait_trident_craft",
        "krait_focus_craft",
        "rare_gear",
        "rare_gear_salvage",
        "gear_salvage",
        "common_gear_salvage",
    };

    static const inline std::set<std::string> FORGE_COMMANDS = {
        "symbol_enh_forge",
        "charm_brilliance_forge",
        "lodestone_forge",
    };

    static const inline std::set<std::string> RUNE_COMMANDS = {
        "scholar_rune",
        "dragonhunter_rune",
        "guardian_rune",
    };

    static const inline std::set<std::string> SIGIL_COMMANDS = {
        "sigil_of_impact",
        "sigil_of_doom",
        "sigil_of_torment",
        "sigil_of_bursting",
        "sigil_of_paralyzation",
    };

    static const inline std::set<std::string> RELIC_COMMANDS = {
        "relic_of_fireworks",
        "relic_of_thief",
        "relic_of_aristocracy",
    };

    static const inline std::set<std::string> COMMANDS = []()
    {
        std::set<std::string> combined;
        combined.insert(OTHER_COMMANDS.begin(), OTHER_COMMANDS.end());
        combined.insert(GEAR_COMMANDS.begin(), GEAR_COMMANDS.end());
        combined.insert(FORGE_COMMANDS.begin(), FORGE_COMMANDS.end());
        combined.insert(RUNE_COMMANDS.begin(), RUNE_COMMANDS.end());
        combined.insert(SIGIL_COMMANDS.begin(), SIGIL_COMMANDS.end());
        combined.insert(RELIC_COMMANDS.begin(), RELIC_COMMANDS.end());
        return combined;
    }();

    enum class PriceCategory
    {
        Other,
        Rune,
        Forge,
        Sigil,
        Relic,
    };

    /* category for every predefined table, used to drive the Prices tab filter */
    static const inline std::map<std::string, PriceCategory> COMMAND_CATEGORY = []()
    {
        std::map<std::string, PriceCategory> mapping;
        for (const auto &command : OTHER_COMMANDS)
            mapping[command] = PriceCategory::Other;
        for (const auto &command : GEAR_COMMANDS)
            mapping[command] = PriceCategory::Other;
        for (const auto &command : FORGE_COMMANDS)
            mapping[command] = PriceCategory::Forge;
        for (const auto &command : RUNE_COMMANDS)
            mapping[command] = PriceCategory::Rune;
        for (const auto &command : SIGIL_COMMANDS)
            mapping[command] = PriceCategory::Sigil;
        for (const auto &command : RELIC_COMMANDS)
            mapping[command] = PriceCategory::Relic;
        return mapping;
    }();

    static const inline std::array<const char *, 4> KRAIT_CRAFT_NAMES = {
        "crafting_cost",
        "ecto_sell_after_tax",
        "profit",
        "tooltip_str",
    };

    static const inline std::array<const char *, 4> CRAFT_NAMES = {
        "crafting_cost",
        "sell",
        "profit",
        "tooltip_str",
    };

    static const inline std::array<const char *, 4> RARE_GEAR_NAMES = {
        "stack_buy",
        "salvage_costs",
        "mats_value_after_tax",
        "profit_stack",
    };

    static const inline std::array<const char *, 4> GEAR_SALVAGE_NAMES = {
        "stack_buy",
        "salvage_costs",
        "mats_value_after_tax",
        "profit_stack",
    };

    static const inline std::array<const char *, 8> T5_MATS_BUY_NAMES = {
        "large_claw",
        "potent_blood",
        "large_bone",
        "intricate_totem",
        "large_fang",
        "potent_venom",
        "large_scale",
        "tooltip_str",
    };

    static const inline std::array<const char *, 7> MATS_CRAFTING_COMPARE_NAMES = {
        "mithril_ore_to_ingot",
        "mithril_ingot_buy",
        "elder_wood_log_to_plank",
        "elder_wood_plank_buy",
        "lucent_mote_to_crystal",
        "lucent_crystal_buy",
        "tooltip_str",
    };

    static const inline std::array<const char *, 4> COMMON_GEAR_NAMES = {
        "stack_buy",
        "salvage_costs",
        "mats_value_after_tax",
        "profit_stack",
    };

    static const inline std::array<const char *, 4> LODESTONE_NAMES = {
        "onyx",
        "charged",
        "corrupted",
        "destroyer",
    };

    static const inline std::array<const char *, 4> THESIS_MASTERFUL_MALICE = CRAFT_NAMES;

    static const inline std::array<const char *, 4> SCHOLAR_RUNE_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> GUARDIAN_RUNE_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> DRAGONHUNTER_RUNE_NAMES = CRAFT_NAMES;

    static const inline std::array<const char *, 4> FIREWORKS_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> THIEF_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> ARISTOCRACY_NAMES = CRAFT_NAMES;

    static const inline std::array<const char *, 4> SIGIL_OF_IMPACT_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> SIGIL_OF_DOOM_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> SIGIL_OF_TORMENT_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> SIGIL_OF_BURSTING_NAMES = CRAFT_NAMES;
    static const inline std::array<const char *, 4> SIGIL_OF_PARALYZATION_NAMES = CRAFT_NAMES;

    static const inline std::array<const char *, 4> KRAIT_SHIELD_CRAFT_NAMES = KRAIT_CRAFT_NAMES;
    static const inline std::array<const char *, 4> KRAIT_TRIDENT_CRAFT_NAMES = KRAIT_CRAFT_NAMES;
    static const inline std::array<const char *, 4> KRAIT_FOCUS_CRAFT_NAMES = KRAIT_CRAFT_NAMES;

    static const inline std::array<const char *, 3> FORGE_ENH_NAMES = {
        "cost",
        "profit_per_try",
        "profit_per_shard",
    };

    static const inline std::array<const char *, 3> FORGE_CHARM_NAMES = {
        "cost",
        "profit_per_try",
        "profit_per_shard",
    };
};
