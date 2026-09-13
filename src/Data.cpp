#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>

#include "nlohmann/json.hpp"

#include "httpclient/httpclient.h"

#include "API.h"
#include "Constants.h"
#include "Data.h"
#include "Settings.h"

using json = nlohmann::json;

static OrderedIntValues _collect_json(const json &jval, const std::string &prefix)
{
    OrderedIntValues kv;

    if (jval.is_object())
    {
        for (auto it = jval.begin(); it != jval.end(); ++it)
        {
            auto _kv = _collect_json(it.value(), prefix.empty() ? it.key() : prefix + "." + it.key());
            kv.insert(kv.end(), _kv.begin(), _kv.end());
        }
    }
    else if (jval.is_array())
    {
        for (size_t i = 0; i < jval.size(); ++i)
        {
            auto _kv = _collect_json(jval[i], prefix + "[" + std::to_string(i) + "]");
            kv.insert(kv.end(), _kv.begin(), _kv.end());
        }
    }
    else
    {
        if (jval.is_number_integer())
        {
            kv.emplace_back(prefix, static_cast<int>(jval));
        }
    }

    return kv;
}

static OrderedStringValues _collect_json_strings(const json &jval, const std::string &prefix)
{
    OrderedStringValues kv;

    if (jval.is_object())
    {
        for (auto it = jval.begin(); it != jval.end(); ++it)
        {
            auto _kv = _collect_json_strings(it.value(), prefix.empty() ? it.key() : prefix + "." + it.key());
            kv.insert(kv.end(), _kv.begin(), _kv.end());
        }
    }
    else if (jval.is_array())
    {
        for (size_t i = 0; i < jval.size(); ++i)
        {
            auto _kv = _collect_json_strings(jval[i], prefix + "[" + std::to_string(i) + "]");
            kv.insert(kv.end(), _kv.begin(), _kv.end());
        }
    }
    else if (jval.is_string())
    {
        kv.emplace_back(prefix, jval.get<std::string>());
    }

    return kv;
}

void Data::requesting()
{
    if (!requested)
    {
        std::wcout << "Requesting data from API...\n";
        futures.clear();

        const auto &base_url = API::LOCAL_API_URL;

        for (auto command : API::COMMANDS)
        {
            if (command == "ecto")
                command = "price?item_id=19721";
            else if (command == "rare_gear")
                command = "price?item_id=83008";
            else if (command == "krait_shield_craft" && Settings::EctoRate != 0.90F)
                command = "krait_shield_craft?ecto_rate=" + std::to_string(Settings::EctoRate);
            else if (command == "krait_trident_craft" && Settings::EctoRate != 0.90F)
                command = "krait_trident_craft?ecto_rate=" + std::to_string(Settings::EctoRate);
            else if (command == "krait_focus_craft" && Settings::EctoRate != 0.90F)
                command = "krait_focus_craft?ecto_rate=" + std::to_string(Settings::EctoRate);

            const auto wstr_url = base_url + L"/" + std::wstring(command.begin(), command.end());
            auto future = HTTPClient::GetRequestAsync(wstr_url);
            auto req = Request(std::move(command), std::move(future));
            futures.push_back(std::move(req));
        }

        for (const auto &custom_item : Settings::CustomItems)
        {
            auto request_id = "custom_" + std::to_string(custom_item.item_id);
            const auto wstr_url = base_url + L"/price?item_id=" + std::to_wstring(custom_item.item_id);
            auto future = HTTPClient::GetRequestAsync(wstr_url);
            auto req = Request(std::move(request_id), std::move(future));
            futures.push_back(std::move(req));
        }

        requested = true;
        api_data.clear();
        api_string_data.clear();
    }
}

void Data::storing()
{
    if (futures.size() == 0)
        loaded = true;
    else
        loaded = false;

    auto it = futures.begin();

    while (it != futures.end())
    {
        if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto j = json{};
            auto request_id = it->request_id;
            std::string request;
            try
            {
                request = it->future.get();
                j = json::parse(request);
            }
            catch (const std::exception &e)
            {
                std::cerr << "Request failed for request_id '" << request_id << "': " << e.what() << std::endl;
                it = futures.erase(it);
                return;
            }

            if (request_id == "price?item_id=19721")
                request_id = "ecto";
            else if (request_id == "price?item_id=83008")
                request_id = "rare_gear";
            else if (request_id.find("krait_shield_craft") != std::string::npos)
                request_id = "krait_shield_craft";
            else if (request_id.find("krait_trident_craft") != std::string::npos)
                request_id = "krait_trident_craft";
            else if (request_id.find("krait_focus_craft") != std::string::npos)
                request_id = "krait_focus_craft";

            auto kv = _collect_json(j, "");
            auto string_kv = _collect_json_strings(j, "");

            api_data[request_id] = kv;
            api_string_data[request_id] = string_kv;
            it = futures.erase(it);
            return; /* return early in this frame */
        }

        ++it;
    }
}

static std::vector<MyOrderEntry> _parse_transactions(const json &jarr)
{
    auto entries = std::vector<MyOrderEntry>{};

    for (const auto &item : jarr)
    {
        entries.push_back(MyOrderEntry{
            .item_id = item.value("item_id", 0),
            .item_name = std::string{},
            .quantity = item.value("quantity", 0),
            .my_price = item.value("price", 0),
            .curr_price = 0,
        });
    }

    return entries;
}

void Data::request_my_orders()
{
    if (my_orders_requested)
        return;

    my_orders_requested = true;
    my_orders_loaded = false;
    my_orders_error.clear();

    if (Settings::APIKey.empty())
    {
        my_orders_error = "Set an API key in the Settings tab to load your orders.";
        my_orders_buys.clear();
        my_orders_sells.clear();
        my_orders_buys_current.clear();
        my_orders_sells_current.clear();
        my_orders_stage = MyOrdersStage::Idle;
        my_orders_loaded = true;
        return;
    }

    const auto token = std::wstring(Settings::APIKey.begin(), Settings::APIKey.end());
    const std::wstring base = L"https://api.guildwars2.com/v2/commerce/transactions/current/";

    my_orders_buys_future = HTTPClient::GetRequestAsync(base + L"buys?access_token=" + token);
    my_orders_sells_future = HTTPClient::GetRequestAsync(base + L"sells?access_token=" + token);
    my_orders_items_future.reset();
    my_orders_prices_future.reset();
    my_orders_stage = MyOrdersStage::Transactions;
}

void Data::store_my_orders()
{
    if (my_orders_stage == MyOrdersStage::Transactions)
    {
        if (!my_orders_buys_future.has_value() || !my_orders_sells_future.has_value())
            return;
        if (my_orders_buys_future->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        if (my_orders_sells_future->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        try
        {
            my_orders_raw_buys = _parse_transactions(json::parse(my_orders_buys_future->get()));
            my_orders_raw_sells = _parse_transactions(json::parse(my_orders_sells_future->get()));
        }
        catch (const std::exception &e)
        {
            my_orders_error = std::string("Failed to parse transactions: ") + e.what();
            my_orders_buys.clear();
            my_orders_sells.clear();
            my_orders_buys_current.clear();
            my_orders_sells_current.clear();
            my_orders_buys_future.reset();
            my_orders_sells_future.reset();
            my_orders_stage = MyOrdersStage::Idle;
            my_orders_loaded = true;
            return;
        }

        my_orders_buys_future.reset();
        my_orders_sells_future.reset();

        auto ids = std::set<int>{};
        for (const auto &entry : my_orders_raw_buys)
            ids.insert(entry.item_id);
        for (const auto &entry : my_orders_raw_sells)
            ids.insert(entry.item_id);

        if (ids.empty())
        {
            my_orders_error.clear();
            my_orders_buys.clear();
            my_orders_sells.clear();
            my_orders_buys_current.clear();
            my_orders_sells_current.clear();
            my_orders_stage = MyOrdersStage::Idle;
            my_orders_loaded = true;
            return;
        }

        std::wstring ids_param;
        for (const auto id : ids)
        {
            if (!ids_param.empty())
                ids_param += L",";
            ids_param += std::to_wstring(id);
        }

        my_orders_items_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/items?lang=en&ids=" + ids_param);
        my_orders_prices_future = HTTPClient::GetRequestAsync(L"https://api.guildwars2.com/v2/commerce/prices?ids=" + ids_param);
        my_orders_stage = MyOrdersStage::Details;
        return;
    }

    if (my_orders_stage == MyOrdersStage::Details)
    {
        if (!my_orders_items_future.has_value() || !my_orders_prices_future.has_value())
            return;
        if (my_orders_items_future->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        if (my_orders_prices_future->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        try
        {
            const auto items_json = json::parse(my_orders_items_future->get());
            const auto prices_json = json::parse(my_orders_prices_future->get());

            auto names = std::map<int, std::string>{};
            for (const auto &item : items_json)
                names[item.value("id", 0)] = item.value("name", std::string{});

            auto highest_buy = std::map<int, int>{};
            auto lowest_sell = std::map<int, int>{};
            for (const auto &price : prices_json)
            {
                const auto id = price.value("id", 0);
                if (price.contains("buys"))
                    highest_buy[id] = price["buys"].value("unit_price", 0);
                if (price.contains("sells"))
                    lowest_sell[id] = price["sells"].value("unit_price", 0);
            }

            const auto lookup_name = [&](int item_id)
            {
                const auto it = names.find(item_id);
                return it != names.end() ? it->second : std::to_string(item_id);
            };

            /* split orders into outbid/undercut ones and those still leading the market */
            my_orders_buys.clear();
            my_orders_buys_current.clear();
            for (auto entry : my_orders_raw_buys)
            {
                const auto it = highest_buy.find(entry.item_id);
                if (it == highest_buy.end())
                    continue;

                entry.item_name = lookup_name(entry.item_id);
                entry.curr_price = it->second;
                if (entry.curr_price > entry.my_price)
                    my_orders_buys.push_back(std::move(entry));
                else
                    my_orders_buys_current.push_back(std::move(entry));
            }

            my_orders_sells.clear();
            my_orders_sells_current.clear();
            for (auto entry : my_orders_raw_sells)
            {
                const auto it = lowest_sell.find(entry.item_id);
                if (it == lowest_sell.end())
                    continue;

                entry.item_name = lookup_name(entry.item_id);
                entry.curr_price = it->second;
                if (entry.curr_price != 0 && entry.curr_price < entry.my_price)
                    my_orders_sells.push_back(std::move(entry));
                else
                    my_orders_sells_current.push_back(std::move(entry));
            }

            my_orders_error.clear();
        }
        catch (const std::exception &e)
        {
            my_orders_error = std::string("Failed to parse order details: ") + e.what();
            my_orders_buys.clear();
            my_orders_sells.clear();
            my_orders_buys_current.clear();
            my_orders_sells_current.clear();
        }

        my_orders_items_future.reset();
        my_orders_prices_future.reset();
        my_orders_raw_buys.clear();
        my_orders_raw_sells.clear();
        my_orders_stage = MyOrdersStage::Idle;
        my_orders_loaded = true;
    }
}

void Data::shutdown()
{
    for (auto &request : futures)
        request.future.wait();

    futures.clear();

    for (auto *future : {&my_orders_buys_future, &my_orders_sells_future,
                         &my_orders_items_future, &my_orders_prices_future})
    {
        if (future->has_value())
        {
            (*future)->wait();
            future->reset();
        }
    }

    my_orders_stage = MyOrdersStage::Idle;
}
