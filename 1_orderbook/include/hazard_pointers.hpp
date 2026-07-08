#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <cassert>

namespace hft {

// Hazard pointer-based memory reclamation for lock-free data structures
// Enables safe concurrent deletion in multi-threaded scenarios
// Single-threaded consumers can ignore this (but it's here for production safety)

template<typename T>
class HazardPointerDomain {
public:
    static constexpr int RETIRED_THRESHOLD = 2;  // Trigger reclamation after N retirements
    static constexpr int MAX_HAZARD_PTRS = 16;   // Per-thread hazard pointers

    explicit HazardPointerDomain(int num_threads = 1)
        : num_threads_(num_threads),
          hazard_ptrs_(num_threads * MAX_HAZARD_PTRS, std::atomic<T*>(nullptr)) {}

    // Register a pointer as "in use" by current thread
    // Returns a hazard pointer slot index
    int acquire(T* ptr) noexcept {
        int thread_id = get_thread_id();
        int slot = get_free_slot(thread_id);
        if (slot < 0) return -1;  // No free slots

        hazard_ptrs_[thread_id * MAX_HAZARD_PTRS + slot].store(ptr, std::memory_order_release);
        return slot;
    }

    // Release a hazard pointer slot
    void release(int slot) noexcept {
        if (slot < 0) return;
        int thread_id = get_thread_id();
        hazard_ptrs_[thread_id * MAX_HAZARD_PTRS + slot].store(nullptr, std::memory_order_release);
    }

    // Mark a pointer as retired (ready for deletion)
    // May trigger reclamation if threshold is reached
    void retire(T* ptr) noexcept {
        if (!ptr) return;

        retired_.push_back(ptr);
        if (retired_.size() >= RETIRED_THRESHOLD) {
            scan_and_reclaim();
        }
    }

    // Manually trigger reclamation
    void scan_and_reclaim() noexcept {
        if (retired_.empty()) return;

        std::vector<bool> is_hazardous(retired_.size(), false);

        // Scan all hazard pointers to see which retired pointers are still in use
        for (size_t i = 0; i < retired_.size(); ++i) {
            T* retired_ptr = retired_[i];
            for (size_t j = 0; j < hazard_ptrs_.size(); ++j) {
                if (hazard_ptrs_[j].load(std::memory_order_acquire) == retired_ptr) {
                    is_hazardous[i] = true;
                    break;
                }
            }
        }

        // Delete non-hazardous pointers, keep hazardous ones
        std::vector<T*> new_retired;
        for (size_t i = 0; i < retired_.size(); ++i) {
            if (is_hazardous[i]) {
                new_retired.push_back(retired_[i]);
            } else {
                delete retired_[i];  // Safe to reclaim
            }
        }
        retired_ = std::move(new_retired);
    }

    ~HazardPointerDomain() {
        // Clean up any remaining retired pointers
        for (auto ptr : retired_) {
            delete ptr;
        }
    }

private:
    int get_thread_id() const noexcept {
        static thread_local int id = -1;
        if (id < 0) {
            id = std::hash<std::thread::id>{}(std::this_thread::get_id()) % num_threads_;
        }
        return id;
    }

    int get_free_slot(int thread_id) noexcept {
        int base = thread_id * MAX_HAZARD_PTRS;
        for (int i = 0; i < MAX_HAZARD_PTRS; ++i) {
            if (hazard_ptrs_[base + i].load(std::memory_order_acquire) == nullptr) {
                return i;
            }
        }
        return -1;  // No free slots
    }

    int num_threads_;
    std::vector<std::atomic<T*>> hazard_ptrs_;
    std::vector<T*> retired_;  // Node: This is thread-unsafe; use locks in production
};

} // namespace hft
