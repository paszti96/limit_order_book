#pragma once
#include <map>
#include <deque>
#include "common/order.hpp"
#include <optional>

template <typename Compare>
class OrderBook {  
    std::map<price_t, std::deque<Order*>, Compare> book_;        // might try ringbuffer later
    std::map<order_id_t, Order*> order_index_; // for quick order lookup
    std::size_t order_count_;
    

    public:

        bool is_bid_ = false;
        
        OrderBook();

        void add(Order* order);

        bool remove_order(order_id_t order_id);

        std::optional<price_t> best_price() const;

        std::deque<Order*>* best_orders();

        void cleanup(price_t price);


};