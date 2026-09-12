#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

namespace kv::storage {
  struct Entry {
    std::string key;
    std::string value;
    int64_t expire_at_ms{-1};

    [[nodiscard]] bool is_expired(int64_t now_ms) const noexcept {
        return expire_at_ms != -1 && now_ms >= expire_at_ms;
    }

    [[nodiscard]] int64_t ttl_remaining_sec(int64_t now_ms) const noexcept {
        if (expire_at_ms == -1) return -1;
        if (now_ms >= expire_at_ms) return -2;
        return (expire_at_ms - now_ms + 999) / 1000; // Ceil division
    }

     [[nodiscard]] size_t estimated_memory_usage() const noexcept {
        // Base struct + string heap capacities + list node pointers (prev/next: ~24 bytes)
        size_t mem = sizeof(Entry) + 24;
        if (key.capacity() > 15)   mem += key.capacity();   // Beyond SSO threshold
        if (value.capacity() > 15) mem += value.capacity(); // Beyond SSO threshold
        return mem;
    }
  };
  
}