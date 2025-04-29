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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace concurrent_cache {

namespace detail {
template <class K, class V, class Hash, class Eq, class Alloc>
class ConcurrentFixedHashMapImpl;
}

template <class KeyType, class ValueType, typename HashFn = std::hash<KeyType>,
          typename KeyEqual = std::equal_to<KeyType>, typename Allocator = std::allocator<uint8_t>>
class ConcurrentFixedHashMap {
 public:
  typedef KeyType key_type;
  typedef ValueType mapped_type;
  typedef std::pair<const KeyType, ValueType> value_type;
  typedef std::size_t size_type;
  typedef HashFn hasher;
  typedef KeyEqual key_equal;
  using impl_type = detail::ConcurrentFixedHashMapImpl<KeyType, ValueType, HashFn, KeyEqual, Allocator>;
  using iterator = typename impl_type::iterator;
  using const_iterator = typename impl_type::const_iterator;

  explicit ConcurrentFixedHashMap(size_t init_size = 1024 * 1024);
  ~ConcurrentFixedHashMap();

  size_t size() const noexcept;
  size_t capcity() const noexcept;
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

 private:
  detail::ConcurrentFixedHashMapImpl<KeyType, ValueType, HashFn, KeyEqual, Allocator>* impl_ = nullptr;
};
}  // namespace concurrent_cache

#include "concurrent_cache/cache/hashmap_impl.h"