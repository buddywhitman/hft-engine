#pragma once
#include <cstdint>
#include <string_view>
#include <optional>

namespace hft::fix {

constexpr uint8_t SOH = 0x01;

enum class MsgType : uint8_t {
    NewOrderSingle    = 'D',
    OrderCancelReq    = 'F',
    ExecutionReport   = '8',
    Unknown           = 0
};

struct FixMessage {
    MsgType msg_type = MsgType::Unknown;
    std::string_view cl_ord_id;    // tag 11
    std::string_view symbol;       // tag 55
    uint8_t  side = 0;             // tag 54: 1=buy 2=sell
    uint32_t order_qty = 0;        // tag 38
    int64_t  price_ticks = 0;      // tag 44: price * 10000
    uint8_t  ord_type = 0;         // tag 40: 1=market 2=limit
    uint8_t  checksum = 0;         // tag 10
    bool     valid = false;
};

// Parse a FIX message from raw byte buffer.
// Zero-allocation: string_view into original buffer.
// Returns FixMessage with valid=false on parse error.
[[nodiscard]] FixMessage parse(const uint8_t* buf, std::size_t len) noexcept;

// Verify checksum
[[nodiscard]] bool verify_checksum(const uint8_t* buf, std::size_t len) noexcept;

} // namespace hft::fix
