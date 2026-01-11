# pragma once
#include "order_book.hpp"
#include "common/order.hpp"

template <typename Compare>
OrderBook<Compare>::OrderBook() : order_count_(0) {
    if (std::is_same<Compare, std::greater<price_t>>::value) {
        is_bid_ = true;
    } else {
        is_bid_ = false;
    }
}

template <typename Compare>
void OrderBook<Compare>::add(Order* order) {
    if(!order) return;
    
    book_[order->price].push_back(order);
    order_index_[order->order_id] = order;
    order_count_ += order->quantity;
}

template<typename Compare>
bool OrderBook<Compare>::remove_order(order_id_t order_id){
    auto it = order_index_.find(order_id);
    if(it == order_index_.end()){
        return false;
    }

    price_t price_level = it->first;
    auto order_queue = book_[price_level];

    for(std::deque<Order*>::iterator order_it = order_queue.begin(); order_queue!= book_.end();){
        if(order_it->order_id == order_id){
            order_it = order_queue.erase(order_it);
        } else {
            ++order_it;
        }
    }

    order_index_.erase(order_id);
    order_count_--;
    return true;
} 