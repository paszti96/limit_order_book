#pragma once
#include <cstdint>
#include <vector>
#include "common/order.hpp"

enum class OrderStatus : std::uint8_t {
    ACCEPTED = 0,
    FILLED = 1,
    PARTIALLY_FILLED = 2,
    REJECTED = 3,
    CANCELLED = 4
};

struct Trade
{
    std::uint64_t trade_id;
    order_id_t buy_order_id;
    order_id_t sell_order_id;
    std::uint32_t quantity;
    price_t price; // price in cents
    std::uint64_t timestamp; // epoch time in nanoseconds
};


struct OrderResult {
    order_id_t order_id;
    OrderStatus status;
    std::uint32_t filled_quantity;
    std::uint32_t remaining_quantity;
    std::vector<Trade> trades;
};