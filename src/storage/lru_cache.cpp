#include "storage/lru_cache.hpp"
#include <utility>
#include <stdexcept>

namespace kv::storage {

LRUCache::Iterator LRUCache::push_front(Entry entry) {
    list_.push_front(std::move(entry));
    return list_.begin();
}

void LRUCache::move_to_front(Iterator it) noexcept {
    if (it != list_.begin()) {
        // splice moves the node from its current position to the beginning in O(1)
        list_.splice(list_.begin(), list_, it);
    }
}

void LRUCache::erase(Iterator it) noexcept {
    list_.erase(it);
}

Entry LRUCache::pop_tail() {
    if (list_.empty()) {
        throw std::runtime_error("Cannot pop from empty LRU cache");
    }
    Entry victim = std::move(list_.back());
    list_.pop_back();
    return victim;
}

} // namespace kv::storage
