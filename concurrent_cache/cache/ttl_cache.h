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
#include <chrono>
#include "concurrent_cache/cache/cache.h"

namespace concurrent_cache {

template <class KeyType, class ValueType, class HashFn, class KeyEqual, typename Allocator,
          template <typename, typename, typename> class CacheBucket>
class TTLCache {
 public:
  typedef KeyType key_type;
  typedef ValueType mapped_type;
  typedef ValueWithTTL<ValueType> ttl_value_type;
  typedef std::pair<const KeyType, ValueType> value_type;
  typedef std::size_t size_type;
  typedef HashFn hasher;
  typedef KeyEqual key_equal;
  using impl_type = detail::CacheImpl<KeyType, ttl_value_type, HashFn, KeyEqual, Allocator, CacheBucket>;
  using iterator = typename impl_type::iterator;
  using const_iterator = typename impl_type::const_iterator;

  explicit TTLCache(const CacheOptions& options = {});

  size_t size() const noexcept;
  size_t capcity() const noexcept;
  size_t bucket_count() const noexcept;
  bool empty() const noexcept;
  const_iterator find(const KeyType& k);
  size_t erase(const KeyType& k);

  template <typename Key, typename Value, typename DURATION>
  std::pair<const_iterator, bool> insert(Key&& k, Value&& v, DURATION ttl);

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

  ~TTLCache();

 private:
  impl_type* impl_ = nullptr;
};

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::TTLCache(const CacheOptions& options) {
  impl_ = new detail::CacheImpl<K, ttl_value_type, Hash, Eq, Alloc, CacheBucket>(options);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::~TTLCache() {
  delete impl_;
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::find(const K& k) {
  return impl_->filter_find(k, [](const auto& v) { return v.pttl() > 0; });
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
size_t TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::erase(const K& k) {
  return impl_->erase(k);
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
template <typename Key, typename Value, typename DURATION>
std::pair<typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator, bool>
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::insert(Key&& k, Value&& v, DURATION ttl) {
  auto ttl_val = make_ttl_value(std::move(v), ttl);
  return impl_->emplace(std::move(k), std::move(ttl_val));
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
template <typename... Args>
bool TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::insert_or_assign(Args&&... args) {
  return impl_->insert_or_assign(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
template <typename... Args>
std::optional<typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator>
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::assign(Args&&... args) {
  return impl_->assign(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
template <typename Key, typename Value, typename Predicate>
std::optional<typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator>
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::assign_if(Key&& k, Value&& desired, Predicate&& predicate) {
  return impl_->assign_if(std::move(k), std::move(desired), std::forward<Predicate>(predicate));
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
template <typename Predicate>
size_t TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::erase_key_if(const key_type& k, Predicate&& predicate) {
  return impl_->erase_key_if(k, std::forward<Predicate>(predicate));
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::begin() const noexcept {
  return impl_->begin();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::end() const noexcept {
  return impl_->end();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::cbegin() const noexcept {
  return impl_->cbegin();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
typename TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::cend() const noexcept {
  return impl_->cend();
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
size_t TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::size() const noexcept {
  return impl_->size();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
size_t TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::capcity() const noexcept {
  return impl_->capcity();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
size_t TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::bucket_count() const noexcept {
  return impl_->bucket_count();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename> class CacheBucket>
std::string TTLCache<K, V, Hash, Eq, Alloc, CacheBucket>::stats() const {
  return impl_->stats();
}

template <class KeyType, class ValueType, class HashFn = absl::container_internal::hash_default_hash<KeyType>,
          class KeyEqual = absl::container_internal::hash_default_eq<KeyType>,
          typename Allocator = std::allocator<uint8_t>>
using LRUTTLCache = TTLCache<KeyType, ValueType, HashFn, KeyEqual, Allocator, detail::LRUBucket>;

template <class KeyType, class ValueType, class HashFn = absl::container_internal::hash_default_hash<KeyType>,
          class KeyEqual = absl::container_internal::hash_default_eq<KeyType>,
          typename Allocator = std::allocator<uint8_t>>
using LFUTTLCache = TTLCache<KeyType, ValueType, HashFn, KeyEqual, Allocator, detail::LFUBucket>;

template <class KeyType, class ValueType, class HashFn = absl::container_internal::hash_default_hash<KeyType>,
          class KeyEqual = absl::container_internal::hash_default_eq<KeyType>,
          typename Allocator = std::allocator<uint8_t>>
using FIFOTTLCache = TTLCache<KeyType, ValueType, HashFn, KeyEqual, Allocator, detail::FIFOBucket>;

}  // namespace concurrent_cache