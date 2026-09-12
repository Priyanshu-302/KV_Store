#pragma once

#include "storage/lru_cache.hpp"
#include "common/types.hpp"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <optional>
#include <cstdint>
#include <cstddef>

namespace kv::storage {

/**
 * @brief Primary In-Memory Key-Value Storage Subsystem.
 * 
 * Synchronizes std::unordered_map with LRUCache to guarantee amortized $O(1)$
 * key lookups, updates, and tail evictions under memory limits.
 */
class Engine {
public:
    explicit Engine(size_t max_memory_bytes);
    ~Engine() = default;

    // Redis Command Primitives
    StatusCode set(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    [[nodiscard]] std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    [[nodiscard]] bool exists(const std::string& key);
    [[nodiscard]] int64_t ttl(const std::string& key);

    /// Flushes all entries and clears memory
    void flushdb() noexcept;

    /// Active randomized sampling TTL eviction loop (called by Server cron)
    size_t active_expire_cycle(size_t sample_size = 20, int64_t max_duration_ms = 25);

    /// Enforces maxmemory boundary by evicting LRU tail nodes
    bool enforce_memory_limit();

    [[nodiscard]] size_t current_memory_usage() const noexcept { return current_memory_bytes_; }
    [[nodiscard]] size_t key_count() const noexcept { return index_.size(); }
    [[nodiscard]] size_t max_memory() const noexcept { return max_memory_bytes_; }

private:
    /// Lazy expiration check invoked on access (GET, DEL, EXISTS)
    bool check_and_expire_passive(const std::string& key);

    /// Evicts exactly one LRU tail entry; returns reclaimed memory in bytes
    size_t evict_one_entry();

    size_t max_memory_bytes_{0};
    size_t current_memory_bytes_{0};

    // Composite storage containers
    LRUCache lru_list_;
    std::unordered_map<std::string, LRUCache::Iterator> index_;
    std::unordered_set<std::string> ttl_keys_; // Secondary index for active TTL sampling
};

} // namespace kv::storage
