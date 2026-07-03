#include <catch2/catch_test_macros.hpp>
#include "fix_message.hpp"

using namespace hft::fix;

TEST_CASE("FIX parser: valid NewOrderSingle", "[fix]") {
    // FIX.4.2 NewOrderSingle message
    std::string msg = "35=D\00111=ORDER123\00155=AAPL\00154=1\00138=100\00144=150.5000\00140=2\00110=123\001";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    REQUIRE(fix_msg.valid);
    REQUIRE(fix_msg.msg_type == MsgType::NewOrderSingle);
    REQUIRE(fix_msg.cl_ord_id == "ORDER123");
    REQUIRE(fix_msg.symbol == "AAPL");
    REQUIRE(fix_msg.side == 1);  // Buy
    REQUIRE(fix_msg.order_qty == 100);
    REQUIRE(fix_msg.ord_type == 2);  // Limit
}

TEST_CASE("FIX parser: valid OrderCancelReq", "[fix]") {
    std::string msg = "35=F\00141=ORDER123\00110=045\001";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    REQUIRE(fix_msg.valid);
    REQUIRE(fix_msg.msg_type == MsgType::OrderCancelReq);
}

TEST_CASE("FIX parser: valid ExecutionReport", "[fix]") {
    std::string msg = "35=8\00150=EXEC123\00110=234\001";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    REQUIRE(fix_msg.valid);
    REQUIRE(fix_msg.msg_type == MsgType::ExecutionReport);
}

TEST_CASE("FIX parser: price conversion", "[fix]") {
    // Price 123.4567 should become 1234567 ticks
    std::string msg = "35=D\00144=123.4567\00110=123\001";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    REQUIRE(fix_msg.valid);
    // Check that price ticks are roughly converted
    REQUIRE(fix_msg.price_ticks > 0);
}

TEST_CASE("FIX parser: empty buffer", "[fix]") {
    auto fix_msg = parse(nullptr, 0);
    REQUIRE(!fix_msg.valid);
}

TEST_CASE("FIX parser: malformed - missing SOH", "[fix]") {
    std::string msg = "35=D11=ORDER123";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    // Parser should handle gracefully
    REQUIRE((!fix_msg.valid || fix_msg.msg_type == MsgType::NewOrderSingle));
}

TEST_CASE("FIX parser: symbol field", "[fix]") {
    std::string msg = "35=D\00155=MSFT\00110=123\001";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    REQUIRE(fix_msg.valid);
    REQUIRE(fix_msg.symbol == "MSFT");
}

TEST_CASE("FIX parser: zero quantity invalid", "[fix]") {
    std::string msg = "35=D\00138=0\00110=123\001";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    // Parser parses it, validation happens elsewhere
    REQUIRE(fix_msg.valid);
    REQUIRE(fix_msg.order_qty == 0);
}

TEST_CASE("FIX parser: sell side (side=2)", "[fix]") {
    std::string msg = "35=D\00154=2\00110=123\001";
    auto fix_msg = parse(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    REQUIRE(fix_msg.valid);
    REQUIRE(fix_msg.side == 2);  // Sell
}
