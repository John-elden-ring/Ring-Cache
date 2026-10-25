#include "ringcache/cache_node.hpp"

#include <utility>

namespace ringcache {

CacheNode::CacheNode(std::string id, std::size_t capacity)
    : id_(std::move(id)),
      cache_(capacity) {}

const std::string& CacheNode::getId() const noexcept {
    return id_;
}

bool CacheNode::put(const std::string& key, const std::string& value) {
    return cache_.put(key, value);
}

std::optional<std::string> CacheNode::get(const std::string& key) {
    return cache_.get(key);
}

bool CacheNode::remove(const std::string& key) {
    return cache_.remove(key);
}

bool CacheNode::contains(const std::string& key) const {
    return cache_.contains(key);
}

std::optional<std::string> CacheNode::peek(const std::string& key) const {
    return cache_.peek(key);
}

std::size_t CacheNode::size() const {
    return cache_.size();
}

std::size_t CacheNode::capacity() const noexcept {
    return cache_.capacity();
}

bool CacheNode::empty() const {
    return cache_.empty();
}

void CacheNode::clear() {
    cache_.clear();
}

} // namespace ringcache
