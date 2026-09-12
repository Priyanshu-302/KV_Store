#pragma once

#include "storage/entry.hpp"
#include <list>
#include <cstddef>

namespace kv::storage {
  class LRUCache {
  public:
    using ListType = std::list<Entry>;
    using Iterator = ListType::iterator;
    using ConstIterator = ListType::const_iterator;

    LRUCache() = default;
    ~LRUCache() = default;

    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    Iterator push_front(Entry entry);

    void move_to_front(Iterator it) noexcept;

    void erase(Iterator it) noexcept;

    Entry pop_tail();

    [[nodiscard]] size_t size() const noexcept { return list_.size(); }
    [[nodiscard]] bool empty() const noexcept { return list_.empty(); }

    Iterator begin() noexcept { return list_.begin(); }

    Iterator end() noexcept { return list_.end(); }

  private:
    ListType list_;
  };
  
}