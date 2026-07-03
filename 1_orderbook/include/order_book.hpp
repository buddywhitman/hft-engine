#pragma once
#include "order.hpp"
#include "price_level.hpp"
#include "spsc_queue.hpp"
#include "tsc_clock.hpp"
#include <array>
#include <functional>
#include <optional>

namespace hft {

constexpr std::size_t MAX_ORDERS = 1 << 20;  // 1M orders, ~64MB

struct MatchResult {
    uint64_t aggressor_id;
    uint64_t resting_id;
    int64_t  price_ticks;
    uint32_t quantity;
    uint64_t match_timestamp_tsc;
};

// Lock-free order book with two price ladders (bids/asks).
// Matching logic: new buy order matches against asks where ask <= price;
// new sell order matches against bids where bid >= price.
// Execution price: resting order's price (maker price).
class OrderBook {
public:
    using MatchCallback = std::function<void(const MatchResult&)>;

    explicit OrderBook(uint32_t symbol_id, MatchCallback on_match);

    // Returns order_id. Triggers on_match callback for each fill.
    // Target: P99 < 200ns from call to return on pinned core.
    uint64_t add_order(Side side, int64_t price_ticks,
                       uint32_t quantity, OrderType type) noexcept;

    bool cancel_order(uint64_t order_id) noexcept;

    [[nodiscard]] const PriceLevel* best_bid() const noexcept {
        return bids_.best();
    }

    [[nodiscard]] const PriceLevel* best_ask() const noexcept {
        return asks_.worst();
    }

    [[nodiscard]] int64_t spread_ticks() const noexcept {
        const auto* bid = best_bid();
        const auto* ask = best_ask();
        if (!bid || !ask)
            return INT64_MAX;
        return ask->price_ticks - bid->price_ticks;
    }

    [[nodiscard]] uint64_t total_orders() const noexcept { return next_order_id_ - 1; }

private:
    uint64_t match_aggressive_buy(uint32_t quantity, int64_t price_ticks) noexcept;
    uint64_t match_aggressive_sell(uint32_t quantity, int64_t price_ticks) noexcept;

    uint32_t symbol_id_;
    MatchCallback on_match_;
    FlatPriceLadder bids_;  // buy side
    FlatPriceLadder asks_;  // sell side

    // Memory pool: no heap allocation in hot path
    std::vector<Order> order_pool_;
    SpscQueue<uint32_t, MAX_ORDERS> free_list_;

    // Order ID map for cancellations (simplified: array indexed by order_id & mask)
    std::vector<uint32_t> order_id_to_slot_;

    uint64_t next_order_id_ = 1;
};

} // namespace hft
