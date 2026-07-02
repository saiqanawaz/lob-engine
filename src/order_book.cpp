#include "lob/order_book.hpp"

namespace lob {

void PriceLevel::push_back(Order* o) {
    o->level = this;
    o->prev  = tail;
    o->next  = nullptr;
    if (tail) {
        tail->next = o;
    } else {
        head = o;
    }
    tail = o;
    total_qty += o->quantity;
    ++order_count;
}

void PriceLevel::remove(Order* o) {
    if (o->prev) {
        o->prev->next = o->next;
    } else {
        head = o->next;
    }
    if (o->next) {
        o->next->prev = o->prev;
    } else {
        tail = o->prev;
    }
    total_qty -= o->quantity;
    --order_count;
    o->prev  = nullptr;
    o->next  = nullptr;
    o->level = nullptr;
}

Order* OrderBook::allocate(const Order& proto) {
    Order* o;
    if (!free_list_.empty()) {
        o = free_list_.back();
        free_list_.pop_back();
    } else {
        pool_.emplace_back();
        o = &pool_.back();
    }
    *o       = proto;
    o->prev  = nullptr;
    o->next  = nullptr;
    o->level = nullptr;
    return o;
}

void OrderBook::release(Order* o) { free_list_.push_back(o); }

Order* OrderBook::insert(const Order& order) {
    if (by_id_.contains(order.id)) return nullptr;
    Order* o = allocate(order);

    PriceLevel* level;
    if (o->side == Side::Buy) {
        level = &bids_.try_emplace(o->price).first->second;
    } else {
        level = &asks_.try_emplace(o->price).first->second;
    }
    if (level->order_count == 0) level->price = o->price;
    level->push_back(o);
    by_id_.emplace(o->id, o);
    return o;
}

void OrderBook::unlink(Order* o) {
    PriceLevel* level = o->level;
    const Price price = level->price;
    const Side  side  = o->side;
    level->remove(o);
    if (level->order_count == 0) {
        if (side == Side::Buy) {
            bids_.erase(price);
        } else {
            asks_.erase(price);
        }
    }
}

bool OrderBook::remove(OrderId id) {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) return false;
    Order* o = it->second;
    unlink(o);
    by_id_.erase(it);
    release(o);
    return true;
}

void OrderBook::erase(Order* o) {
    unlink(o);
    by_id_.erase(o->id);
    release(o);
}

void OrderBook::reduce(Order* o, Quantity delta) {
    o->quantity -= delta;
    o->level->total_qty -= delta;
}

Order* OrderBook::find(OrderId id) {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : it->second;
}

const Order* OrderBook::find(OrderId id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : it->second;
}

const PriceLevel* OrderBook::best_bid() const {
    return bids_.empty() ? nullptr : &bids_.begin()->second;
}

const PriceLevel* OrderBook::best_ask() const {
    return asks_.empty() ? nullptr : &asks_.begin()->second;
}

}  // namespace lob
