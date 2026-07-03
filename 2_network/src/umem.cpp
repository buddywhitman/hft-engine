#include "umem.hpp"
#include <sys/mman.h>
#include <cstring>

namespace hft::net {

Umem::Umem(uint32_t num_frames, uint32_t frame_size)
    : num_frames_(num_frames), frame_size_(frame_size) {
    uint64_t total = static_cast<uint64_t>(num_frames) * frame_size;

    // Allocate with mmap for better alignment
    base_ = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base_ == MAP_FAILED)
        base_ = nullptr;
    else
        std::memset(base_, 0, total);
}

Umem::~Umem() {
    if (base_) {
        uint64_t total = static_cast<uint64_t>(num_frames_) * frame_size_;
        munmap(base_, total);
    }
}

} // namespace hft::net
