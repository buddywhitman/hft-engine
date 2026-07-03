#pragma once
#include <atomic>
#include <optional>
#include <cstddef>

namespace hft {

// Lock-free multi-producer, single consumer queue using a linked list.
// Each producer atomically swaps its node into the tail; consumer drains from head.
// Uses Vyukov's MPSC algorithm with acquire/release semantics.

template<typename T>
class MpscQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next{nullptr};
    };

public:
    MpscQueue() {
        auto stub = new Node();
        head_.store(stub, std::memory_order_relaxed);
        tail_.store(stub, std::memory_order_relaxed);
    }

    ~MpscQueue() {
        Node* node = head_.load(std::memory_order_relaxed);
        while (node != nullptr) {
            Node* next = node->next.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }
    }

    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;

    // Multiple threads can call this concurrently
    void push(const T& item) noexcept {
        auto node = new Node{item};
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }

    // Only single consumer thread can call this
    [[nodiscard]] std::optional<T> pop() noexcept {
        Node* head = head_.load(std::memory_order_relaxed);
        Node* next = head->next.load(std::memory_order_acquire);
        if (next == nullptr)
            return std::nullopt;

        T data = next->data;
        head_.store(next, std::memory_order_relaxed);
        delete head;
        return data;
    }

private:
    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::atomic<Node*> tail_;
};

} // namespace hft
