#include <benchmark/benchmark.h>
#include "fix_message.hpp"
#include "order_book.hpp"
#include "tsc_clock.hpp"
#include <vector>
#include <string>

using namespace hft;
using namespace hft::fix;

// Generate a synthetic FIX message string
static std::string make_fix_message(int order_id, int qty, int price, char side) {
    std::string msg = "8=FIX.4.2\0019=65\00135=D\00149=SENDER\00156=TARGET\00134=";
    msg += std::to_string(order_id);
    msg += "\00111=ORDER";
    msg += std::to_string(order_id);
    msg += "\00155=AAPL\00154=";
    msg += side;  // 1=buy, 2=sell
    msg += "\00138=";
    msg += std::to_string(qty);
    msg += "\00144=";
    msg += std::to_string(price / 10000);
    msg += ".";
    msg += std::to_string((price % 10000) / 1000);
    msg += std::to_string((price % 1000) / 100);
    msg += std::to_string((price % 100) / 10);
    msg += std::to_string(price % 10);
    msg += "\00140=2\00110=123\001";
    return msg;
}

static void fix_parser_throughput(benchmark::State& state) {
    // Generate 10M synthetic messages
    std::vector<std::string> messages;
    for (int i = 0; i < 10000; ++i) {
        messages.push_back(make_fix_message(i, 100, 150000, '1'));
    }

    for (auto _ : state) {
        for (const auto& msg_str : messages) {
            auto fix_msg = parse(
                reinterpret_cast<const uint8_t*>(msg_str.c_str()),
                msg_str.size()
            );
            asm volatile("" : : "r"(&fix_msg) : "memory");
        }
    }

    state.SetItemsProcessed(messages.size());
}
BENCHMARK(fix_parser_throughput)->Unit(benchmark::kMillisecond);

static void fix_parse_with_orderbook(benchmark::State& state) {
    TscClock::calibrate();

    std::vector<std::string> messages;
    for (int i = 0; i < 10000; ++i) {
        messages.push_back(make_fix_message(i, 100, 150000 + i, (i % 2) ? '1' : '2'));
    }

    std::vector<MatchResult> matches;
    OrderBook ob(1, [&](const MatchResult& m) { matches.push_back(m); });

    std::vector<uint64_t> latencies;
    latencies.reserve(100000);

    for (auto _ : state) {
        for (const auto& msg_str : messages) {
            uint64_t tsc_start = TscClock::now();

            auto fix_msg = parse(
                reinterpret_cast<const uint8_t*>(msg_str.c_str()),
                msg_str.size()
            );

            if (fix_msg.valid && fix_msg.msg_type == MsgType::NewOrderSingle) {
                Side side = (fix_msg.side == 1) ? Side::Buy : Side::Sell;
                ob.add_order(side, fix_msg.price_ticks, fix_msg.order_qty, OrderType::Limit);
            }

            uint64_t tsc_end = TscClock::now();
            uint64_t latency_ns = TscClock::tsc_to_ns(tsc_end - tsc_start);

            if (latencies.size() < latencies.capacity())
                latencies.push_back(latency_ns);
        }
    }

    state.SetItemsProcessed(messages.size());
}
BENCHMARK(fix_parse_with_orderbook)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
