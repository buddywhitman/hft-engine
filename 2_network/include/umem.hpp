#pragma once
#include <cstdint>
#include <cstddef>

namespace hft::net {

// UMEM (User Memory) for AF_XDP: contiguous region split into fixed-size frames.
// Four rings coordinate ownership: FILL (userspace->kernel), RX (kernel->userspace),
// TX, and COMPLETION.
// Zero-copy mode: NIC writes directly into UMEM via DMA.

class Umem {
public:
    explicit Umem(uint32_t num_frames = 4096, uint32_t frame_size = 2048);
    ~Umem();

    Umem(const Umem&) = delete;
    Umem& operator=(const Umem&) = delete;

    [[nodiscard]] void* base_addr() const noexcept { return base_; }
    [[nodiscard]] uint32_t num_frames() const noexcept { return num_frames_; }
    [[nodiscard]] uint32_t frame_size() const noexcept { return frame_size_; }
    [[nodiscard]] uint64_t total_size() const noexcept {
        return static_cast<uint64_t>(num_frames_) * frame_size_;
    }

    // Get pointer to frame at offset
    [[nodiscard]] uint8_t* frame_at_offset(uint64_t offset) const noexcept {
        return reinterpret_cast<uint8_t*>(base_) + offset;
    }

private:
    void* base_;
    uint32_t num_frames_;
    uint32_t frame_size_;
};

} // namespace hft::net
