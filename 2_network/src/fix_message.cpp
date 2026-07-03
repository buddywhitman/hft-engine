#include "fix_message.hpp"
#include <cstring>

namespace hft::fix {

// Helpers
static uint8_t ascii_to_digit(uint8_t c) {
    return (c >= '0' && c <= '9') ? c - '0' : 255;
}

static bool is_digit(uint8_t c) {
    return c >= '0' && c <= '9';
}

FixMessage parse(const uint8_t* buf, std::size_t len) noexcept {
    FixMessage msg;

    if (!buf || len == 0)
        return msg;

    std::size_t pos = 0;

    // Parse tag=value pairs separated by SOH
    while (pos < len) {
        // Parse tag (numeric, 1-5 digits)
        uint32_t tag = 0;
        while (pos < len && is_digit(buf[pos])) {
            tag = tag * 10 + ascii_to_digit(buf[pos]);
            pos++;
        }

        if (pos >= len || buf[pos] != '=')
            return msg;
        pos++;  // skip '='

        // Parse value (until SOH)
        const uint8_t* val_start = buf + pos;
        while (pos < len && buf[pos] != SOH) {
            pos++;
        }
        const uint8_t* val_end = buf + pos;
        std::string_view value(reinterpret_cast<const char*>(val_start),
                              val_end - val_start);

        // Dispatch on tag
        switch (tag) {
            case 35:  // MsgType
                if (value.size() == 1) {
                    msg.msg_type = static_cast<MsgType>(value[0]);
                }
                break;
            case 11:  // ClOrdID
                msg.cl_ord_id = value;
                break;
            case 55:  // Symbol
                msg.symbol = value;
                break;
            case 54:  // Side
                if (value.size() > 0)
                    msg.side = value[0] - '0';
                break;
            case 38:  // OrderQty
                msg.order_qty = 0;
                for (char c : value) {
                    msg.order_qty = msg.order_qty * 10 + (c - '0');
                }
                break;
            case 44:  // Price (multiply by 10000 to convert to ticks)
            {
                int64_t price_int = 0;
                int decimal_places = 0;
                bool in_decimal = false;
                for (char c : value) {
                    if (c == '.') {
                        in_decimal = true;
                    } else if (is_digit(c)) {
                        price_int = price_int * 10 + (c - '0');
                        if (in_decimal)
                            decimal_places++;
                    }
                }
                // Assume 4 decimal places for standard tick size
                while (decimal_places < 4) {
                    price_int *= 10;
                    decimal_places++;
                }
                msg.price_ticks = price_int;
            }
            break;
            case 40:  // OrdType
                if (value.size() > 0)
                    msg.ord_type = value[0] - '0';
                break;
            case 10:  // Checksum
                if (value.size() > 0)
                    msg.checksum = ascii_to_digit(value[0]) * 10 +
                                  ascii_to_digit(value[1]);
                break;
        }

        if (pos >= len)
            break;
        pos++;  // skip SOH
    }

    msg.valid = true;
    return msg;
}

bool verify_checksum(const uint8_t* buf, std::size_t len) noexcept {
    if (!buf || len < 10)
        return false;

    // Find checksum field (tag 10)
    std::size_t checksum_pos = len - 1;
    while (checksum_pos > 0 && buf[checksum_pos] != SOH)
        checksum_pos--;

    // Calculate checksum from start to before checksum field
    uint8_t sum = 0;
    for (std::size_t i = 0; i < checksum_pos; ++i)
        sum += buf[i];

    // Extract stored checksum
    if (checksum_pos < 4)
        return false;
    std::size_t checksum_val_start = checksum_pos - 3;
    std::string_view checksum_str(reinterpret_cast<const char*>(buf + checksum_val_start), 3);

    uint8_t stored_checksum = (ascii_to_digit(checksum_str[0]) * 100 +
                              ascii_to_digit(checksum_str[1]) * 10 +
                              ascii_to_digit(checksum_str[2])) % 256;

    return sum == stored_checksum;
}

} // namespace hft::fix
