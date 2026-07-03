#pragma once
#include <cstdint>

namespace hft {

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : uint8_t { Limit = 0, Market = 1, Cancel = 2 };

// Must fit in exactly one cache line (64 bytes).
// Every order book read must hit L1 cache with a single load.
struct alignas(64) Order {
    uint64_t order_id;        // 8 bytes
    uint64_t timestamp_tsc;   // 8 bytes
    int64_t  price_ticks;     // 8 bytes (price * 10000, negative for sells)
    uint32_t quantity;        // 4 bytes
    uint32_t filled_qty;      // 4 bytes
    uint32_t symbol_id;       // 4 bytes (interned string ID)
    Side     side;            // 1 byte
    OrderType type;           // 1 byte
    uint8_t  padding[26];     // 26 bytes padding
};

static_assert(sizeof(Order) == 64, "Order must be exactly 64 bytes");
static_assert(alignof(Order) == 64, "Order must be cache-line aligned");

} // namespace hft
