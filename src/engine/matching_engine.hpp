#pragma once
#include <functional>
#include "book/order_book.hpp"
#include "pool/order_pool.hpp"
#include "common/order.hpp"
#include "common/order_result.hpp"

class MatchingEngine {
private: 
    OrderBook<std::greater<price_t>> bids_;
    OrderBook<std::less<price_t>> asks_;
    OrderPool order_pool_;
    
    OrderResult match_order(Order* incoming_order);

    // callbacks
    void on_trade(std::function<void(const Trade&)>);
    void on_order(std::function<void(const Order&)>);   

public:
    MatchingEngine() = default;

    OrderResult submit_order(order_id_t order_id, Side side, OrderType type, uint32_t quantity, price_t price, uint64_t timestamp);

    OrderResult cancel_order(order_id_t order_id);
};