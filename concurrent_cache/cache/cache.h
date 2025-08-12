/*
 * Copyright (c) 2025 qiyingwang <qiyingwang@tencent.com>. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "concurrent_cache/cache/cache_impl.h"
#include "concurrent_cache/cache/options.h"
#include "concurrent_cache/common/time.h"

namespace concurrent_cache {

template <class KeyType, class ValueType, class HashFn, class KeyEqual, typename Allocator,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
class Cache {
 public:
  typedef KeyType key_type;
  typedef ValueType mapped_type;
  typedef std::pair<const KeyType, ValueType> value_type;
  typedef std::size_t size_type;
  typedef HashFn hasher;
  typedef KeyEqual key_equal;
  using impl_type = detail::CacheImpl<KeyType, ValueType, HashFn, KeyEqual, Allocator, CacheBucket>;
  using iterator = typename impl_type::iterator;
  using const_iterator = typename impl_type::const_iterator;

  explicit Cache(const CacheOptions& options = {});

  size_t size() const noexcept;
  size_t capacity() const noexcept;
  size_t bucket_count() const noexcept;
  bool empty() const noexcept;
  const_iterator find(const KeyType& k) const;
  size_t erase(const KeyType& k);
  template <typename... Args>
  std::pair<const_iterator, bool> emplace(Args&&... args);
  template <typename Key, typename Value>
  std::pair<const_iterator, bool> insert(Key&& k, Value&& v) {
    return emplace(std::move(k), std::move(v));
  }

  template <typename... Args>
  bool insert_or_assign(Args&&... args);
  template <typename... Args>
  std::optional<const_iterator> assign(Args&&... args);

  template <typename Key, typename Value, typename Predicate>
  std::optional<const_iterator> assign_if(Key&& k, Value&& desired, Predicate&& predicate);
  template <typename Key, typename Value>
  std::optional<const_iterator> assign_if_equal(Key&& k, const ValueType& expected, Value&& desired) {
    return assign_if(k, std::move(desired), [&expected](const ValueType& v) { return v == expected; });
  }

  template <typename Predicate>
  size_type erase_key_if(const key_type& k, Predicate&& predicate);
  size_type erase_if_equal(const key_type& k, const ValueType& expected) {
    return erase_key_if(k, [&expected](const ValueType& v) { return v == expected; });
  }
  const_iterator cend() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator begin() const noexcept;

  std::string stats() const;

  ~Cache();

 private:
  impl_type* impl_ = nullptr;
};

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
Cache<K, V, Hash, Eq, Alloc, CacheBucket>::Cache(const CacheOptions& options) {
  impl_ = new detail::CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>(options);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
Cache<K, V, Hash, Eq, Alloc, CacheBucket>::~Cache() {
  delete impl_;
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator Cache<K, V, Hash, Eq, Alloc, CacheBucket>::find(
    const K& k) const {
  return impl_->find(k);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t Cache<K, V, Hash, Eq, Alloc, CacheBucket>::erase(const K& k) {
  return impl_->erase(k);
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename... Args>
std::pair<typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator, bool>
Cache<K, V, Hash, Eq, Alloc, CacheBucket>::emplace(Args&&... args) {
  return impl_->emplace(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename... Args>
bool Cache<K, V, Hash, Eq, Alloc, CacheBucket>::insert_or_assign(Args&&... args) {
  return impl_->insert_or_assign(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename... Args>
std::optional<typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator>
Cache<K, V, Hash, Eq, Alloc, CacheBucket>::assign(Args&&... args) {
  return impl_->assign(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename Key, typename Value, typename Predicate>
std::optional<typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator>
Cache<K, V, Hash, Eq, Alloc, CacheBucket>::assign_if(Key&& k, Value&& desired, Predicate&& predicate) {
  return impl_->assign_if(std::move(k), std::move(desired), std::forward<Predicate>(predicate));
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename Predicate>
size_t Cache<K, V, Hash, Eq, Alloc, CacheBucket>::erase_key_if(const key_type& k, Predicate&& predicate) {
  return impl_->erase_key_if(k, std::forward<Predicate>(predicate));
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator Cache<K, V, Hash, Eq, Alloc, CacheBucket>::begin()
    const noexcept {
  return impl_->begin();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator Cache<K, V, Hash, Eq, Alloc, CacheBucket>::end()
    const noexcept {
  return impl_->end();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator Cache<K, V, Hash, Eq, Alloc, CacheBucket>::cbegin()
    const noexcept {
  return impl_->cbegin();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename Cache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator Cache<K, V, Hash, Eq, Alloc, CacheBucket>::cend()
    const noexcept {
  return impl_->cend();
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t Cache<K, V, Hash, Eq, Alloc, CacheBucket>::size() const noexcept {
  return impl_->size();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t Cache<K, V, Hash, Eq, Alloc, CacheBucket>::capacity() const noexcept {
  return impl_->capacity();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t Cache<K, V, Hash, Eq, Alloc, CacheBucket>::bucket_count() const noexcept {
  return impl_->bucket_count();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
std::string Cache<K, V, Hash, Eq, Alloc, CacheBucket>::stats() const {
  return impl_->stats();
}

template <class KeyType, class ValueType, class HashFn = absl::container_internal::hash_default_hash<KeyType>,
          class KeyEqual = absl::container_internal::hash_default_eq<KeyType>,
          typename Allocator = std::allocator<uint8_t>>
using LRUCache = Cache<KeyType, ValueType, HashFn, KeyEqual, Allocator, detail::LRUBucket>;

template <class KeyType, class ValueType, class HashFn = absl::container_internal::hash_default_hash<KeyType>,
          class KeyEqual = absl::container_internal::hash_default_eq<KeyType>,
          typename Allocator = std::allocator<uint8_t>>
using LFUCache = Cache<KeyType, ValueType, HashFn, KeyEqual, Allocator, detail::LFUBucket>;

template <class KeyType, class ValueType, class HashFn = absl::container_internal::hash_default_hash<KeyType>,
          class KeyEqual = absl::container_internal::hash_default_eq<KeyType>,
          typename Allocator = std::allocator<uint8_t>>
using FIFOCache = Cache<KeyType, ValueType, HashFn, KeyEqual, Allocator, detail::FIFOBucket>;

}  // namespace concurrent_cache
