#include "ringcache/lru_cache.hpp"

namespace ringcache {

// Explicit template instantiation for standard string-based cache in the static library
template class BasicLruCache<std::string, std::string>;

} // namespace ringcache
