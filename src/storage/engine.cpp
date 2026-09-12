#include "storage/engine.hpp"
#include "common/logger.hpp"
#include <chrono>

namespace kv::storage {

// Approximate per-node bucket overhead in std::unordered_map
constexpr size_t HASH_MAP_NODE_OVERHEAD = 32;

Engine::Engine(size_t max_memory_bytes)
    : max_memory_bytes_(max_memory_bytes) {}

bool Engine::check_and_expire_passive(const std::string& key) {
    auto it = index_.find(key);
    if (it == index_.end()) return false;

    if (it->second->is_expired(current_time_ms())) {
        // Key expired -> purge from storage immediately
        del(key);
        return true;
    }
    return false;
}

StatusCode Engine::set(const std::string& key, const std::string& value, int64_t ttl_ms) {
    int64_t expire_at = -1;
    if (ttl_ms > 0) {
        expire_at = current_time_ms() + ttl_ms;
    }

    auto map_it = index_.find(key);

    if (map_it != index_.end()) {
        // --- Key Update Path ---
        auto list_it = map_it->second;
        size_t old_entry_mem = list_it->estimated_memory_usage();

        list_it->value = value;
        list_it->expire_at_ms = expire_at;
        lru_list_.move_to_front(list_it);

        size_t new_entry_mem = list_it->estimated_memory_usage();
        if (new_entry_mem > old_entry_mem) {
            current_memory_bytes_ += (new_entry_mem - old_entry_mem);
        } else {
            current_memory_bytes_ -= (old_entry_mem - new_entry_mem);
        }
    } else {
        // --- New Key Insertion Path ---
        Entry new_entry{key, value, expire_at};
        size_t entry_mem = new_entry.estimated_memory_usage() + HASH_MAP_NODE_OVERHEAD;

        // Check and enforce memory limit before insertion
        if (max_memory_bytes_ > 0 && (current_memory_bytes_ + entry_mem) > max_memory_bytes_) {
            if (!enforce_memory_limit()) {
                KV_LOG_WARN("Memory limit reached ({} bytes). OOM on SET.", current_memory_bytes_);
                return StatusCode::ERR_OOM;
            }
        }

        auto list_it = lru_list_.push_front(std::move(new_entry));
        index_[key] = list_it;
        current_memory_bytes_ += entry_mem;
    }

    // Maintain secondary active TTL index
    if (expire_at > 0) {
        ttl_keys_.insert(key);
    } else {
        ttl_keys_.erase(key);
    }

    return StatusCode::OK;
}

std::optional<std::string> Engine::get(const std::string& key) {
    // 1. Check passive expiration
    if (check_and_expire_passive(key)) {
        return std::nullopt;
    }

    auto map_it = index_.find(key);
    if (map_it == index_.end()) {
        return std::nullopt;
    }

    // 2. Promote node to MRU head in O(1)
    lru_list_.move_to_front(map_it->second);
    return map_it->second->value;
}

bool Engine::del(const std::string& key) {
    auto map_it = index_.find(key);
    if (map_it == index_.end()) {
        return false;
    }

    auto list_it = map_it->second;
    size_t reclaimed_mem = list_it->estimated_memory_usage() + HASH_MAP_NODE_OVERHEAD;

    lru_list_.erase(list_it);
    index_.erase(map_it);
    ttl_keys_.erase(key);

    if (current_memory_bytes_ >= reclaimed_mem) {
        current_memory_bytes_ -= reclaimed_mem;
    } else {
        current_memory_bytes_ = 0;
    }

    return true;
}

bool Engine::exists(const std::string& key) {
    if (check_and_expire_passive(key)) {
        return false;
    }
    return index_.find(key) != index_.end();
}

int64_t Engine::ttl(const std::string& key) {
    if (check_and_expire_passive(key)) {
        return -2; // Not found / expired
    }

    auto map_it = index_.find(key);
    if (map_it == index_.end()) {
        return -2;
    }

    return map_it->second->ttl_remaining_sec(current_time_ms());
}

void Engine::flushdb() noexcept {
    while (!lru_list_.empty()) {
        lru_list_.pop_tail();
    }
    index_.clear();
    ttl_keys_.clear();
    current_memory_bytes_ = 0;
}

size_t Engine::evict_one_entry() {
    if (lru_list_.empty()) return 0;

    Entry victim = lru_list_.pop_tail();
    index_.erase(victim.key);
    ttl_keys_.erase(victim.key);

    size_t reclaimed = victim.estimated_memory_usage() + HASH_MAP_NODE_OVERHEAD;
    if (current_memory_bytes_ >= reclaimed) {
        current_memory_bytes_ -= reclaimed;
    } else {
        current_memory_bytes_ = 0;
    }

    KV_LOG_DEBUG("Evicted LRU tail key: '{}' (reclaimed {} bytes)", victim.key, reclaimed);
    return reclaimed;
}

bool Engine::enforce_memory_limit() {
    if (max_memory_bytes_ == 0) return true;

    // Evict items until current usage drops below threshold
    while (current_memory_bytes_ >= max_memory_bytes_) {
        if (lru_list_.empty()) {
            return false; // Unable to reclaim memory
        }
        evict_one_entry();
    }
    return true;
}

size_t Engine::active_expire_cycle(size_t sample_size, int64_t max_duration_ms) {
    if (ttl_keys_.empty()) return 0;

    size_t total_expired = 0;
    int64_t start_time = monotonic_time_ms();
    int64_t now_wall = current_time_ms();

    while (true) {
        size_t sampled = 0;
        size_t expired_in_batch = 0;
        std::vector<std::string> keys_to_delete;

        // Sample up to sample_size random keys
        for (const auto& key : ttl_keys_) {
            sampled++;
            auto it = index_.find(key);
            if (it != index_.end() && it->second->is_expired(now_wall)) {
                keys_to_delete.push_back(key);
                expired_in_batch++;
            }
            if (sampled >= sample_size) break;
        }

        // Delete expired keys
        for (const auto& key : keys_to_delete) {
            del(key);
            total_expired++;
        }

        // Adaptive stop: if <= 25% were expired, stop early
        if (sampled == 0 || (expired_in_batch * 4) <= sampled) {
            break;
        }

        // Hard circuit breaker: break if execution exceeded max_duration_ms
        if ((monotonic_time_ms() - start_time) >= max_duration_ms) {
            KV_LOG_DEBUG("Active TTL cycle hit {}ms latency circuit breaker.", max_duration_ms);
            break;
        }
    }

    return total_expired;
}

} // namespace kv::storage
