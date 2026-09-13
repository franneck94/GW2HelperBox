#pragma once

#include <future>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Types.h"

class Data
{
public:
    bool requested = false;
    bool loaded = false;

    std::list<Request> futures;
    std::map<std::string, OrderedIntValues> api_data;
    std::map<std::string, OrderedStringValues> api_string_data;

    void requesting();
    void storing();

    bool my_orders_requested = false;
    bool my_orders_loaded = false;
    std::string my_orders_error;
    std::vector<MyOrderEntry> my_orders_buys;
    std::vector<MyOrderEntry> my_orders_sells;
    /* orders that are still the best on the market (highest bid / lowest sell) */
    std::vector<MyOrderEntry> my_orders_buys_current;
    std::vector<MyOrderEntry> my_orders_sells_current;

    void request_my_orders();
    void store_my_orders();
    void shutdown();

private:
    /* fetching orders from the official GW2 API is a two-stage pipeline: first the current
       transactions, then the item names and market prices needed to display and filter them */
    enum class MyOrdersStage
    {
        Idle,
        Transactions,
        Details,
    };

    MyOrdersStage my_orders_stage = MyOrdersStage::Idle;

    std::vector<MyOrderEntry> my_orders_raw_buys;
    std::vector<MyOrderEntry> my_orders_raw_sells;

    std::optional<std::future<std::string>> my_orders_buys_future;
    std::optional<std::future<std::string>> my_orders_sells_future;
    std::optional<std::future<std::string>> my_orders_items_future;
    std::optional<std::future<std::string>> my_orders_prices_future;
};
