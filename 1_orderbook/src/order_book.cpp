#include "order_book.hpp"
#include <cstring>

namespace hft {

OrderBook::OrderBook(uint32_t symbol_id, MatchCallback on_match)
    : symbol_id_(symbol_id), on_match_(std::move(on_match)) {
    // Pre-allocate memory pools
    order_pool_.resize(MAX_ORDERS);
    order_id_to_slot_.resize(MAX_ORDERS, 0xFFFFFFFF);

    // Initialize free list with all order slots
    for (uint32_t i = 0; i < MAX_ORDERS; ++i) {
        [[maybe_unused]] bool pushed = free_list_.push(i);
        // Safe to ignore: queue is empty during init
    }
}

uint64_t OrderBook::add_order(Side side, int64_t price_ticks,
                              uint32_t quantity, OrderType type) noexcept {
    uint64_t order_id = next_order_id_++;

    if (type == OrderType::Cancel) {
        cancel_order(price_ticks);  // Use price_ticks as order_id to cancel
        return order_id;
    }

    // Allocate from pool
    auto slot_opt = free_list_.pop();
    if (!slot_opt)
        return 0;  // Pool exhausted

    uint32_t slot = *slot_opt;
    Order& order = order_pool_[slot];
    order.order_id = order_id;
    order.timestamp_tsc = TscClock::now();
    order.price_ticks = price_ticks;
    order.quantity = quantity;
    order.filled_qty = 0;
    order.symbol_id = symbol_id_;
    order.side = side;
    order.type = type;

    order_id_to_slot_[order_id & (MAX_ORDERS - 1)] = slot;

    // Try to match immediately
    uint32_t remaining = quantity;
    if (side == Side::Buy)
        remaining = match_aggressive_buy(remaining, price_ticks);
    else
        remaining = match_aggressive_sell(remaining, price_ticks);

    // If fully filled, return to pool
    if (remaining == 0) {
        [[maybe_unused]] bool pushed = free_list_.push(slot);
        // Safe to ignore: queue has space during order lifecycle
        return order_id;
    }

    // Add resting part to book
    order.quantity = remaining;
    if (side == Side::Buy)
        bids_.upsert(price_ticks, remaining);
    else
        asks_.upsert(price_ticks, remaining);

    return order_id;
}

uint64_t OrderBook::match_aggressive_buy(uint32_t quantity, int64_t price_ticks) noexcept {
    while (quantity > 0) {
        const auto* ask = asks_.worst();
        if (!ask || ask->price_ticks > price_ticks)
            break;

        // Execute at ask price
        uint32_t fill_qty = std::min(quantity, static_cast<uint32_t>(ask->total_quantity));
        on_match_({0, 0, ask->price_ticks, fill_qty, TscClock::now()});

        quantity -= fill_qty;
        auto* ask_mut = const_cast<PriceLevel*>(ask);
        ask_mut->total_quantity -= fill_qty;
        asks_.remove_if_empty(ask->price_ticks);
    }
    return quantity;
}

uint64_t OrderBook::match_aggressive_sell(uint32_t quantity, int64_t price_ticks) noexcept {
    while (quantity > 0) {
        const auto* bid = bids_.best();
        if (!bid || bid->price_ticks < price_ticks)
            break;

        uint32_t fill_qty = std::min(quantity, static_cast<uint32_t>(bid->total_quantity));
        on_match_({0, 0, bid->price_ticks, fill_qty, TscClock::now()});

        quantity -= fill_qty;
        auto* bid_mut = const_cast<PriceLevel*>(bid);
        bid_mut->total_quantity -= fill_qty;
        bids_.remove_if_empty(bid->price_ticks);
    }
    return quantity;
}

bool OrderBook::cancel_order(uint64_t order_id) noexcept {
    uint32_t slot = order_id_to_slot_[order_id & (MAX_ORDERS - 1)];
    if (slot >= MAX_ORDERS)
        return false;

    Order& order = order_pool_[slot];
    if (order.order_id != order_id)
        return false;

    if (order.side == Side::Buy)
        bids_.remove_if_empty(order.price_ticks);
    else
        asks_.remove_if_empty(order.price_ticks);

    [[maybe_unused]] bool pushed = free_list_.push(slot);
    // Safe to ignore: queue has space during order lifecycle
    return true;
}

} // namespace hft
