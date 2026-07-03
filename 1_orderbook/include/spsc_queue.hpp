#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>
#include <cassert>

namespace hft {

// Wait-free single producer, single consumer queue.
// Every element read hits L1/L2 in the hot path. Fits entirely in L1.
// False sharing between head_ and tail_ would cause ~60-100ns coherency penalty;
// separate cache lines (alignas(64)) eliminate this entirely.
template<typename T, std::size_t Capacity>
    requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0)
class SpscQueue {
public:
    SpscQueue() : head_(0), tail_(0) {}

    [[nodiscard]] bool push(const T& item) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire))
            return false;
        data_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::optional<T> pop() noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return std::nullopt;
        T item = data_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const auto h = head_.load(std::memory_order_acquire);
        const auto t = tail_.load(std::memory_order_acquire);
        return (h - t) & mask_;
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
    alignas(64) std::array<T, Capacity> data_;
};

} // namespace hft
