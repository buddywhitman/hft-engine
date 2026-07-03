#pragma once
#include "order.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace hft {

struct PriceLevel {
    int64_t  price_ticks;
    uint64_t total_quantity;
    uint32_t order_count;
    uint32_t padding;
};

// Flat sorted array of price levels.
// Binary search O(log n), insert/delete O(n).
// Cache-optimal for order books with <200 active price levels.
// At n=200, flat binary search (8 comparisons in L1) beats std::map
// (8 pointer chases across DRAM, ~480ns) by ~60x.
class FlatPriceLadder {
public:
    explicit FlatPriceLadder(std::size_t reserve = 256) {
        levels_.reserve(reserve);
    }

    PriceLevel* find(int64_t price_ticks) noexcept {
        auto it = std::lower_bound(
            levels_.begin(), levels_.end(), price_ticks,
            [](const PriceLevel& l, int64_t p) { return l.price_ticks < p; }
        );
        if (it != levels_.end() && it->price_ticks == price_ticks)
            return &*it;
        return nullptr;
    }

    const PriceLevel* find(int64_t price_ticks) const noexcept {
        auto it = std::lower_bound(
            levels_.begin(), levels_.end(), price_ticks,
            [](const PriceLevel& l, int64_t p) { return l.price_ticks < p; }
        );
        if (it != levels_.end() && it->price_ticks == price_ticks)
            return &*it;
        return nullptr;
    }

    PriceLevel& upsert(int64_t price_ticks, uint32_t qty_delta) noexcept {
        auto it = std::lower_bound(
            levels_.begin(), levels_.end(), price_ticks,
            [](const PriceLevel& l, int64_t p) { return l.price_ticks < p; }
        );

        if (it != levels_.end() && it->price_ticks == price_ticks) {
            it->total_quantity += qty_delta;
            it->order_count++;
            return *it;
        }

        it = levels_.insert(it, PriceLevel{price_ticks, qty_delta, 1, 0});
        return *it;
    }

    void remove_if_empty(int64_t price_ticks) noexcept {
        auto it = std::lower_bound(
            levels_.begin(), levels_.end(), price_ticks,
            [](const PriceLevel& l, int64_t p) { return l.price_ticks < p; }
        );
        if (it != levels_.end() && it->price_ticks == price_ticks && it->total_quantity == 0) {
            levels_.erase(it);
        }
    }

    [[nodiscard]] const PriceLevel* best() const noexcept {
        return levels_.empty() ? nullptr : &levels_.back();
    }

    [[nodiscard]] const PriceLevel* worst() const noexcept {
        return levels_.empty() ? nullptr : &levels_.front();
    }

    [[nodiscard]] std::size_t size() const noexcept { return levels_.size(); }
    [[nodiscard]] bool empty() const noexcept { return levels_.empty(); }

private:
    std::vector<PriceLevel> levels_;
};

} // namespace hft
