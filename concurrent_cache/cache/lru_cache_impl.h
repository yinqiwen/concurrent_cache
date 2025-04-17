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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/lang/SafeAssert.h>
#include <folly/synchronization/Hazptr.h>
#include "detail/node.h"
#include "folly/ThreadCachedInt.h"
#include "folly/synchronization/DistributedMutex.h"

#include "concurrent_cache/cache/detail/hazptr.h"
#include "concurrent_cache/cache/detail/iterator.h"
#include "concurrent_cache/cache/detail/node.h"
#include "concurrent_cache/common/allign.h"
#include "concurrent_cache/common/pool.h"
#include "concurrent_cache/common/time.h"
#include "concurrent_cache/simd/simd_ops.h"

namespace concurrent_cache {
namespace detail {
template <class KeyType, class ValueType, typename HashFn, typename KeyEqual, typename Allocator>
class LRUCacheImpl {
 public:
  typedef KeyType key_type;
  typedef ValueType mapped_type;
  typedef std::pair<const KeyType, ValueType> value_type;
  typedef std::size_t size_type;
  typedef HashFn hasher;
  typedef KeyEqual key_equal;
  using allocator_type = Allocator;
  using bucket_type = LRUBucket<KeyType, ValueType, Allocator>;
  using Self = LRUCacheImpl<KeyType, ValueType, HashFn, KeyEqual, Allocator>;
  using iterator = Iterator<Self>;
  using const_iterator = Iterator<Self>;

  explicit LRUCacheImpl(const LRUCacheOptions& options);
  ~LRUCacheImpl();

  size_t bucket_count() const;
  const_iterator cend() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator begin() const noexcept;

  const_iterator find(const KeyType& k) const;
  template <typename... Args>
  std::pair<const_iterator, bool> emplace(Args&&... args);
  template <typename... Args>
  bool insert_or_assign(Args&&... args);
  template <typename... Args>
  std::optional<const_iterator> assign(Args&&... args);
  size_type erase(const key_type& k);

  template <typename Key, typename Value, typename Predicate>
  std::optional<const_iterator> assign_if(Key&& k, Value&& desired, Predicate&& predicate);
  template <typename Predicate>
  size_type erase_key_if(const key_type& k, Predicate&& predicate);

  size_t size() const;
  size_t capacity() const;

  std::string stats() const;

 private:
  static constexpr size_t kDefaultMaxSize = 1024 * 1024;
  static constexpr size_t kMaxEvictRetry = 4;
  using bucket_mutex_t = folly::DistributedMutex;
  using node_type = typename iterator::Node;
  enum class InsertType {
    DOES_NOT_EXIST,  // insert/emplace operations.  If key exists, return false.
    MUST_EXIST,      // assign operations.  If key does not exist, return false.
    ANY,             // insert_or_assign.
    MATCH,           // assign_if_equal (not in std).  For concurrent maps, a
                     // way to atomically change a value if equal to some other
                     // value.
  };

  struct BucketMeta {
    bucket_mutex_t lock;
    std::atomic<uint16_t> size{0};
    std::atomic<uint16_t> capacity{0};
    std::atomic<bool> evicting{false};
  };

  void inc_size(size_t bucket_idx) {
    ++size_;
    ++bucket_metas_[bucket_idx].size;
  }
  void dec_size(size_t bucket_idx) {
    --size_;
    --bucket_metas_[bucket_idx].size;
  }

  void try_evict(size_t bucket_idx);

  template <typename T, typename... Args>
  T* create(Args&&... args) {
    T* memory = reinterpret_cast<T*>(Allocator().allocate(sizeof(T)));
    new (memory) T(std::forward<Args>(args)...);
    if constexpr (std::is_same_v<node_type, T>) {
      record_alive_node_counter(true);
    }
    return memory;
  }
  template <typename T>
  void destroy(T* p) {
    p->~T();
    Allocator().deallocate((uint8_t*)p, sizeof(T));
    if constexpr (std::is_same_v<node_type, T>) {
      record_alive_node_counter(false);
    }
  }

  std::pair<uint32_t, uint8_t> get_idx_and_tag(const key_type& key) const;

  bool find_slot(iterator& iter, const key_type& key, uint8_t tag) const;

  bool acquire_empty_slot(iterator& iter, uint8_t tag, node_type* node);
  template <typename MatchFunc>
  size_t erase_slot(iterator& iter, const key_type& key, uint8_t tag, MatchFunc match);
  template <typename MatchFunc>
  bool do_insert(uint32_t bucket_idx, iterator& iter, InsertType type, uint8_t tag, node_type* node, MatchFunc match);

  std::tuple<bucket_type*, uint32_t, node_type*, uint8_t> find_oldest_slot(size_t bucket_idx) const;

  std::unique_lock<bucket_mutex_t> lock_bucket(size_t bucket_idx) {
    return std::unique_lock<bucket_mutex_t>(bucket_metas_[bucket_idx].lock);
  }
  folly::hazptr_obj_cohort<std::atomic>* cohort() {
    static thread_local folly::hazptr_obj_cohort<std::atomic> tls_cohort;
    return &tls_cohort;
    // static __thread uint32_t cursor = 0;
    // uint32_t pool_id = cursor % cohorts_.size();
    // cursor++;
    // return &cohorts_[pool_id];
  }

  LRUCacheOptions opts_;

  // folly::hazptr_obj_cohort<std::atomic> cohort_;
  // std::array<folly::hazptr_obj_cohort<std::atomic>, 8> cohorts_;

  bucket_type* buckets_{nullptr};
  BucketMeta* bucket_metas_{nullptr};
  size_t bucket_count_{0};
  size_t bucket_mask_{0};

  folly::ThreadCachedInt<int64_t> size_{0};
  folly::ThreadCachedInt<int64_t> capacity_{0};
  folly::ThreadCachedInt<int64_t> evict_retry_counter_{0};
  folly::ThreadCachedInt<int64_t> overflow_bucket_size_{0};
};

template <class K, class V, class Hash, class Eq, class Alloc>
LRUCacheImpl<K, V, Hash, Eq, Alloc>::LRUCacheImpl(const LRUCacheOptions& options) : opts_(options) {
  size_t init_size = opts_.max_size;
  if (init_size == 0) {
    init_size = kDefaultMaxSize;
  }

  size_t actual_max_size = static_cast<size_t>(init_size);
  // size_t actual_max_size = static_cast<size_t>(options_.max_size);
  actual_max_size = align_to<size_t>(actual_max_size, kBucketSlotSize);
  bucket_count_ = actual_max_size / kBucketSlotSize;
  if (actual_max_size % kBucketSlotSize > 0) {
    bucket_count_++;
  }
  // bucket_count_ = align_to_next_power_of_two<size_t>(bucket_count_);
  bucket_count_ = next_prime(bucket_count_);
  actual_max_size = bucket_count_ * kBucketSlotSize;
  opts_.max_size = actual_max_size;
  bucket_mask_ = bucket_count_ - 1;
  auto buf = Alloc().allocate(sizeof(bucket_type) * bucket_count_);
  buckets_ = reinterpret_cast<bucket_type*>(buf);
  auto meta_buf = Alloc().allocate(sizeof(BucketMeta) * bucket_count_);
  bucket_metas_ = reinterpret_cast<BucketMeta*>(meta_buf);
  for (size_t i = 0; i < bucket_count_; i++) {
    new (buckets_ + i) bucket_type();
    new (bucket_metas_ + i) BucketMeta();
  }
  capacity_.set(bucket_count_ * kBucketSlotSize);
  // enable_cache_hazptr_thread_pool_executor();
  // cohorts_.resize(8);
  // for (size_t i = 0; i < 8; i++) {
  //   cohorts_.emplace_back(folly::hazptr_obj_cohort<std::atomic>{});
  // }
}
template <class K, class V, class Hash, class Eq, class Alloc>
LRUCacheImpl<K, V, Hash, Eq, Alloc>::~LRUCacheImpl() {
  for (size_t i = 0; i < bucket_count_; i++) {
    buckets_[i].~bucket_type();
    bucket_metas_[i].~BucketMeta();
  }
  Alloc().deallocate((uint8_t*)buckets_, sizeof(bucket_type) * bucket_count_);
  Alloc().deallocate((uint8_t*)bucket_metas_, sizeof(BucketMeta) * bucket_count_);
}
template <class K, class V, class Hash, class Eq, class Alloc>
std::string LRUCacheImpl<K, V, Hash, Eq, Alloc>::stats() const {
  std::string info;
  info.append("node_counter:").append(std::to_string(get_alive_node_counter())).append(",");
  info.append("evict_retry:").append(std::to_string(evict_retry_counter_.readFull())).append(",");
  info.append("root_bucket_size:").append(std::to_string(bucket_count_)).append(",");
  info.append("overflow_bucket_size:").append(std::to_string(overflow_bucket_size_.readFull())).append(",");
  return info;
}

template <class K, class V, class Hash, class Eq, class Alloc>
std::pair<uint32_t, uint8_t> LRUCacheImpl<K, V, Hash, Eq, Alloc>::get_idx_and_tag(const key_type& key) const {
  auto hash = hasher()(key);
  hash ^= hash >> 32;
  hash *= 0x9E3779B97F4A7C15ULL;
  hash ^= hash >> 23;
  uint8_t tag = hash & bucket_type::kTagMask;
  if (tag >= bucket_type::kEmptyCtrl) {
    tag -= 3;
  }
  uint32_t bucket_index = static_cast<uint32_t>((hash >> bucket_type::kTagMaskBits) % bucket_count_);
  return {bucket_index, tag};
}
template <class K, class V, class Hash, class Eq, class Alloc>
bool LRUCacheImpl<K, V, Hash, Eq, Alloc>::acquire_empty_slot(iterator& iter, uint8_t tag, node_type* new_node) {
  auto* bucket = iter.get_bucket();
  auto& hazcurr = iter.hazptr_;
  uint64_t mask =
      simd::simd_vector_match(reinterpret_cast<const uint8_t*>(bucket->tags), kBucketSlotSize, bucket_type::kEmptyCtrl);
  simd::MaskIterator mask_iter(mask);
  while (mask_iter) {
    size_t offset = mask_iter.Advance();
    if (!bucket->acquire_empty(offset)) {
      continue;
    }
    auto& node = bucket->slots[offset];
    auto protect_node = hazcurr.protect(node);
    if (protect_node) {
      FOLLY_SAFE_FATAL("not null for empty slot");
    }
    if (!bucket->set_slot(offset, new_node)) {
      FOLLY_SAFE_FATAL("failed to set new node for empty slot ");
    }
    if (!bucket->set_tag(offset, bucket_type::kBusyCtrl, tag)) {
      FOLLY_SAFE_FATAL("failed to set new tag for empty slot");
    }
    bucket->set_access_timestamp(offset, get_timestamp(opts_.time_scale));
    inc_size(iter.get_bucket_idx());
    hazcurr.reset_protection(new_node);
    return true;
  }
  if (bucket->overflow.load()) {
    iter.set_node(bucket->get_overflow(), 0, nullptr);
    return acquire_empty_slot(iter, tag, new_node);
  } else {
    return false;
  }
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename MatchFunc>
size_t LRUCacheImpl<K, V, Hash, Eq, Alloc>::erase_slot(iterator& iter, const key_type& key, uint8_t tag,
                                                       MatchFunc match) {
  auto* bucket = iter.get_bucket();
  auto& hazcurr = iter.hazptr_;
  uint64_t mask = simd::simd_vector_match(reinterpret_cast<const uint8_t*>(bucket->tags), kBucketSlotSize, tag);
  simd::MaskIterator mask_iter(mask);
  while (mask_iter) {
    size_t offset = mask_iter.Advance();
    if (bucket->tags[offset] != tag) {
      continue;
    }
    auto& node = bucket->slots[offset];
    auto protect_node = hazcurr.protect(node);
    if (protect_node) {
      if (key_equal()(key, protect_node->getItem().first)) {
        if (!match(protect_node->getItem().second)) {
          return 0;
        }
        if (bucket->erase_slot(offset, protect_node, tag)) {
          dec_size(iter.get_bucket_idx());
          return 1;
        }
      } else {
        hazcurr.reset_protection();
      }
    }
  }

  if (bucket->overflow.load()) {
    iter.set_node(bucket->get_overflow(), 0, nullptr);
    return erase_slot(iter, key, tag, match);
  } else {
    return 0;
  }
}

template <class K, class V, class Hash, class Eq, class Alloc>
bool LRUCacheImpl<K, V, Hash, Eq, Alloc>::find_slot(iterator& iter, const K& key, uint8_t tag) const {
  auto* bucket = iter.get_bucket();
  auto& hazcurr = iter.hazptr_;
  uint64_t mask = simd::simd_vector_match(reinterpret_cast<const uint8_t*>(bucket->tags), kBucketSlotSize, tag);
  simd::MaskIterator mask_iter(mask);
  while (mask_iter) {
    size_t offset = mask_iter.Advance();
    if (bucket->tags[offset] != tag) {
      continue;
    }
    auto& node = bucket->slots[offset];
    auto protect_node = hazcurr.protect(node);
    if (protect_node) {
      if (key_equal()(key, protect_node->getItem().first)) {
        iter.set_node(bucket, offset, protect_node);
        return true;
      }
    }
  }
  if (bucket->overflow.load()) {
    iter.set_node(bucket->get_overflow(), 0, nullptr);
    return find_slot(iter, key, tag);
  } else {
    return false;
  }
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename MatchFunc>
bool LRUCacheImpl<K, V, Hash, Eq, Alloc>::do_insert(uint32_t bucket_idx, iterator& iter, InsertType type, uint8_t tag,
                                                    node_type* node, MatchFunc match) {
  auto& hazcurr = iter.hazptr_;
  while (1) {
    if (find_slot(iter, node->getItem().first, tag)) {
      if (type == InsertType::DOES_NOT_EXIST) {
        return false;
      } else {
        if (type == InsertType::MATCH) {
          if (!match(iter.get_node()->getItem().second)) {
            return false;
          }
        }
        auto* bucket = iter.get_bucket();
        auto* current_node = iter.get_node();
        if (bucket->replace_slot(iter.get_bucket_offset(), current_node, node)) {
          iter.set_node(node);
          hazcurr.reset_protection(node);
          if (current_node != nullptr) {
            current_node->retire();
          }
          bucket->set_access_timestamp(iter.get_bucket_offset(), get_timestamp(opts_.time_scale));
          return true;
        } else {
          continue;
        }
      }
    }
    if (type != InsertType::DOES_NOT_EXIST && type != InsertType::ANY) {
      hazcurr.reset_protection();
      return false;
    }

    if (acquire_empty_slot(iter, tag, node)) {
      return true;
    }
    return false;

    // if (bucket_metas_[bucket_idx].capacity.load() >= kBucketSlotSize * 2) {
    //   return false;
    // }

    // auto* new_bucket = create<bucket_type>();
    // new_bucket->set_slot(0, node);
    // new_bucket->tags[0] = tag;
    // new_bucket->set_access_timestamp(0, get_timestamp(opts_.time_scale));
    // if (!iter.get_bucket()->set_overflow(new_bucket)) {
    //   new_bucket->destroy_slot(0, node, false);
    //   destroy(new_bucket);
    //   continue;
    // } else {
    //   capacity_.increment(kBucketSlotSize);
    //   bucket_metas_[bucket_idx].capacity += kBucketSlotSize;
    //   iter.set_node(new_bucket, 0, node);
    //   hazcurr.reset_protection(node);
    //   inc_size(iter.get_bucket_idx());
    //   ++overflow_bucket_size_;
    //   return true;
    // }
  }
}
template <class K, class V, class Hash, class Eq, class Alloc>
std::tuple<typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::bucket_type*, uint32_t,
           typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::node_type*, uint8_t>
LRUCacheImpl<K, V, Hash, Eq, Alloc>::find_oldest_slot(size_t bucket_idx) const {
  auto* bucket = buckets_ + bucket_idx;
  bucket_type* eval_bucket = bucket;
  bucket_type* evict_bucket = nullptr;
  uint32_t oldest_ts = 0;
  uint32_t evict_offset = 0;
  uint8_t evict_slot_tag = 0;
  node_type* evict_node = nullptr;
  while (eval_bucket != nullptr) {
    auto [min_ts, offset] = simd::simd_vector_min(eval_bucket->access_ts, kBucketSlotSize);
    if (min_ts > 0) {
      if (eval_bucket->access_ts[offset] != min_ts) {
        // already updated by other thread, try again
        return {nullptr, 0, nullptr, 0};
      }
      auto tag = eval_bucket->get_tag(offset);
      if (tag >= bucket_type::kEmptyCtrl) {
        return {nullptr, 0, nullptr, 0};
      }
      if (evict_bucket == nullptr || min_ts < oldest_ts) {
        evict_offset = offset;
        evict_bucket = eval_bucket;
        oldest_ts = min_ts;
        evict_slot_tag = tag;
        evict_node = eval_bucket->slots[offset].load();
      }
    }
    eval_bucket = eval_bucket->get_overflow();
  }
  return {evict_bucket, evict_offset, evict_node, evict_slot_tag};
}

template <class K, class V, class Hash, class Eq, class Alloc>
void LRUCacheImpl<K, V, Hash, Eq, Alloc>::try_evict(size_t bucket_idx) {
  auto& bucket_meta = bucket_metas_[bucket_idx];
  auto* bucket = buckets_ + bucket_idx;
  size_t retry = 0;
  bool evicting = false;
  if (!bucket_meta.evicting.compare_exchange_strong(evicting, true)) {
    return;
  }
  while (bucket_meta.size.load() >= kBucketSlotSize && retry < kMaxEvictRetry) {
    retry++;
    ++evict_retry_counter_;
    auto [evict_bucket, evict_offset, evict_node, evict_slot_tag] = find_oldest_slot(bucket_idx);
    if (evict_bucket == nullptr) {
      continue;
    }
    if (evict_bucket->erase_slot(evict_offset, evict_node, evict_slot_tag)) {
      dec_size(bucket_idx);
    }
  }
  bucket_meta.evicting.store(false);
}

template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator LRUCacheImpl<K, V, Hash, Eq, Alloc>::find(
    const K& key) const {
  auto [bucket_index, tag] = get_idx_and_tag(key);
  iterator iter(this, buckets_ + bucket_index, bucket_index, 0);
  if (find_slot(iter, key, tag)) {
    iter.get_bucket()->set_access_timestamp(iter.get_bucket_offset(), get_timestamp(opts_.time_scale));
    return iter;
  } else {
    return end();
  }
}

template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator LRUCacheImpl<K, V, Hash, Eq, Alloc>::begin()
    const noexcept {
  return iterator(this, buckets_, 0);
}

template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator LRUCacheImpl<K, V, Hash, Eq, Alloc>::end() const noexcept {
  return iterator();
}

template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator LRUCacheImpl<K, V, Hash, Eq, Alloc>::cbegin()
    const noexcept {
  return iterator(this, buckets_, 0);
}

template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator LRUCacheImpl<K, V, Hash, Eq, Alloc>::cend()
    const noexcept {
  return iterator();
}

template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCacheImpl<K, V, Hash, Eq, Alloc>::erase(const key_type& key) {
  auto [bucket_index, tag] = get_idx_and_tag(key);
  iterator iter(this, buckets_ + bucket_index, bucket_index, 0);
  auto lock = lock_bucket(bucket_index);
  return erase_slot(iter, key, tag, [](const value_type&) { return true; });
}

template <class K, class V, class Hash, class Eq, class Alloc>
template <typename... Args>
std::pair<typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator, bool>
LRUCacheImpl<K, V, Hash, Eq, Alloc>::emplace(Args&&... args) {
  auto* node = create<node_type>(cohort(), std::forward<Args>(args)...);
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);
  iterator iter(this, buckets_ + bucket_index, bucket_index, 0);
  // auto proxy = bucket_metas_[bucket_index].lock.lock();
  // auto lock = lock_bucket(bucket_index);
  try_evict(bucket_index);
  auto success = do_insert(bucket_index, iter, InsertType::DOES_NOT_EXIST, tag, node, [](const V&) { return false; });
  // bucket_metas_[bucket_index].lock.unlock(proxy);
  if (!success) {
    destroy(node);
    return {std::move(iter), false};
  }
  return {std::move(iter), true};
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename... Args>
bool LRUCacheImpl<K, V, Hash, Eq, Alloc>::insert_or_assign(Args&&... args) {
  auto* node = create<node_type>(cohort(), std::forward<Args>(args)...);
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);
  iterator iter(this, buckets_ + bucket_index, bucket_index, 0);
  // auto lock = lock_bucket(bucket_index);
  while (1) {
    try_evict(bucket_index);
    auto success = do_insert(bucket_index, iter, InsertType::ANY, tag, node, [](const V&) { return false; });
    if (success) {
      break;
    }
  }

  return true;
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename... Args>
std::optional<typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator> LRUCacheImpl<K, V, Hash, Eq, Alloc>::assign(
    Args&&... args) {
  auto* node = create<node_type>(cohort(), std::forward<Args>(args)...);
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);
  iterator iter(this, buckets_ + bucket_index, bucket_index, 0);
  // auto proxy = bucket_metas_[bucket_index].lock.lock();
  // auto lock = lock_bucket(bucket_index);
  auto success = do_insert(bucket_index, iter, InsertType::MUST_EXIST, tag, node, [](const V&) { return false; });
  // bucket_metas_[bucket_index].lock.unlock(proxy);
  if (!success) {
    destroy(node);
    return {};
  }
  return iter;
}

template <class K, class V, class Hash, class Eq, class Alloc>
template <typename Key, typename Value, typename Predicate>
std::optional<typename LRUCacheImpl<K, V, Hash, Eq, Alloc>::const_iterator>
LRUCacheImpl<K, V, Hash, Eq, Alloc>::assign_if(Key&& k, Value&& desired, Predicate&& predicate) {
  auto* node = create<node_type>(cohort(), std::move(k), std::move(desired));
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);
  iterator iter(this, buckets_ + bucket_index, bucket_index, 0);
  // auto proxy = bucket_metas_[bucket_index].lock.lock();
  // auto lock = lock_bucket(bucket_index);
  auto success = do_insert(bucket_index, iter, InsertType::MATCH, tag, node, std::forward<Predicate>(predicate));
  // bucket_metas_[bucket_index].lock.unlock(proxy);
  if (!success) {
    destroy(node);
    return {};
  }
  return iter;
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename Predicate>
size_t LRUCacheImpl<K, V, Hash, Eq, Alloc>::erase_key_if(const key_type& key, Predicate&& predicate) {
  auto [bucket_index, tag] = get_idx_and_tag(key);
  iterator iter(this, buckets_ + bucket_index, bucket_index, 0);
  // auto lock = lock_bucket(bucket_index);
  return erase_slot(iter, key, tag, std::forward<Predicate>(predicate));
}

template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCacheImpl<K, V, Hash, Eq, Alloc>::bucket_count() const {
  return bucket_count_;
}

template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCacheImpl<K, V, Hash, Eq, Alloc>::size() const {
  return reinterpret_cast<size_t>(size_.readFull());
}
template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCacheImpl<K, V, Hash, Eq, Alloc>::capacity() const {
  return reinterpret_cast<size_t>(capacity_.readFull());
}

}  // namespace detail

template <class K, class V, class Hash, class Eq, class Alloc>
LRUCache<K, V, Hash, Eq, Alloc>::LRUCache(const LRUCacheOptions& options) {
  impl_ = new detail::LRUCacheImpl<K, V, Hash, Eq, Alloc>(options);
}
template <class K, class V, class Hash, class Eq, class Alloc>
LRUCache<K, V, Hash, Eq, Alloc>::~LRUCache() {
  delete impl_;
}
template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator LRUCache<K, V, Hash, Eq, Alloc>::find(const K& k) const {
  return impl_->find(k);
}
template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCache<K, V, Hash, Eq, Alloc>::erase(const K& k) {
  return impl_->erase(k);
}

template <class K, class V, class Hash, class Eq, class Alloc>
template <typename... Args>
std::pair<typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator, bool> LRUCache<K, V, Hash, Eq, Alloc>::emplace(
    Args&&... args) {
  return impl_->emplace(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename... Args>
bool LRUCache<K, V, Hash, Eq, Alloc>::insert_or_assign(Args&&... args) {
  return impl_->insert_or_assign(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename... Args>
std::optional<typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator> LRUCache<K, V, Hash, Eq, Alloc>::assign(
    Args&&... args) {
  return impl_->assign(std::forward<Args>(args)...);
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename Key, typename Value, typename Predicate>
std::optional<typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator> LRUCache<K, V, Hash, Eq, Alloc>::assign_if(
    Key&& k, Value&& desired, Predicate&& predicate) {
  return impl_->assign_if(std::move(k), std::move(desired), std::forward<Predicate>(predicate));
}
template <class K, class V, class Hash, class Eq, class Alloc>
template <typename Predicate>
size_t LRUCache<K, V, Hash, Eq, Alloc>::erase_key_if(const key_type& k, Predicate&& predicate) {
  return impl_->erase_key_if(k, std::forward<Predicate>(predicate));
}

template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator LRUCache<K, V, Hash, Eq, Alloc>::begin() const noexcept {
  return impl_->begin();
}
template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator LRUCache<K, V, Hash, Eq, Alloc>::end() const noexcept {
  return impl_->end();
}
template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator LRUCache<K, V, Hash, Eq, Alloc>::cbegin() const noexcept {
  return impl_->cbegin();
}
template <class K, class V, class Hash, class Eq, class Alloc>
typename LRUCache<K, V, Hash, Eq, Alloc>::const_iterator LRUCache<K, V, Hash, Eq, Alloc>::cend() const noexcept {
  return impl_->cend();
}

template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCache<K, V, Hash, Eq, Alloc>::size() const noexcept {
  return impl_->size();
}
template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCache<K, V, Hash, Eq, Alloc>::capcity() const noexcept {
  return impl_->capcity();
}
template <class K, class V, class Hash, class Eq, class Alloc>
size_t LRUCache<K, V, Hash, Eq, Alloc>::bucket_count() const noexcept {
  return impl_->bucket_count();
}
template <class K, class V, class Hash, class Eq, class Alloc>
std::string LRUCache<K, V, Hash, Eq, Alloc>::stats() const {
  return impl_->stats();
}

}  // namespace concurrent_cache
