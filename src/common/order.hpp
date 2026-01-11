#pragma once
#include <cstdint>

using price_t = std::uint32_t;
using order_id_t = std::uint64_t;

enum class Side: std::uint8_t
{
    // class to prohibit cross-enum comparisons
    BUY = 0,
    SELL = 1,
};

enum class OrderType: std::uint8_t
{
    LIMIT = 0,
    MARKET = 1,
};

struct Order
{
    order_id_t order_id;
    Side side;
    OrderType type;
    uint32_t quantity;
    price_t price; // price in cents
    uint64_t timestamp; // epoch time in nanoseconds
    
    bool is_active = false;

    Order() = default;
    Order(order_id_t id, Side s, OrderType t, uint32_t qty, price_t prc, uint64_t ts)
        : order_id(id), side(s), type(t), quantity(qty), price(prc), timestamp(ts), is_active(true) {}
};

