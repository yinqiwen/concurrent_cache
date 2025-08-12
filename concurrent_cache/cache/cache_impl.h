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
template <typename KeyType, typename ValueType, typename HashFn, typename KeyEqual, typename Allocator,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
class CacheImpl {
 public:
  typedef KeyType key_type;
  typedef ValueType mapped_type;
  typedef std::pair<const KeyType, ValueType> value_type;
  typedef std::size_t size_type;
  typedef HashFn hasher;
  typedef KeyEqual key_equal;
  using allocator_type = Allocator;
  // using bucket_type = LRUBucket<KeyType, ValueType, Allocator>;
  using bucket_type = CacheBucket<KeyType, ValueType, Allocator, std::atomic>;
  using Self = CacheImpl<KeyType, ValueType, HashFn, KeyEqual, Allocator, CacheBucket>;
  using iterator = Iterator<Self>;
  using const_iterator = Iterator<Self>;

  explicit CacheImpl(const CacheOptions& options);
  ~CacheImpl();

  size_t bucket_count() const;
  const_iterator cend() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator begin() const noexcept;

  const_iterator find(const KeyType& k) const;

  template <typename Filter>
  const_iterator filter_find(const KeyType& k, Filter&& filter);

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
  template <typename Filter>
  bool filter_find_slot(iterator& iter, const key_type& key, uint8_t tag, Filter&& filter);

  bool acquire_empty_slot(iterator& iter, uint8_t tag, node_type* node);
  template <typename MatchFunc>
  size_t erase_slot(iterator& iter, const key_type& key, uint8_t tag, MatchFunc match);
  template <typename MatchFunc>
  bool do_insert(uint32_t bucket_idx, iterator& iter, InsertType type, uint8_t tag, node_type* node, MatchFunc match);

  std::unique_lock<bucket_mutex_t> lock_bucket(size_t bucket_idx) {
    return std::unique_lock<bucket_mutex_t>(bucket_metas_[bucket_idx].lock);
  }
  folly::hazptr_obj_cohort<std::atomic>* cohort() {
    static thread_local folly::hazptr_obj_cohort<std::atomic> tls_cohort;
    return &tls_cohort;
  }

  CacheOptions opts_;
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

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::CacheImpl(const CacheOptions& options) : opts_(options) {
  size_t init_size = opts_.max_size;
  if (init_size == 0) {
    init_size = kDefaultMaxSize;
  }

  size_t actual_max_size = static_cast<size_t>(init_size);
  actual_max_size =
      static_cast<size_t>(actual_max_size * kBucketSlotSize / (kBucketSlotSize - opts_.bucket_reserved_slots) * 1.0);
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
  enable_estimate_timestamp_updater();
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::~CacheImpl() {
  for (size_t i = 0; i < bucket_count_; i++) {
    buckets_[i].~bucket_type();
    bucket_metas_[i].~BucketMeta();
  }
  Alloc().deallocate((uint8_t*)buckets_, sizeof(bucket_type) * bucket_count_);
  Alloc().deallocate((uint8_t*)bucket_metas_, sizeof(BucketMeta) * bucket_count_);
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
std::string CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::stats() const {
  std::string info;
  info.append("node_counter:").append(std::to_string(get_alive_node_counter())).append(",");
  info.append("evict_retry:").append(std::to_string(evict_retry_counter_.readFull())).append(",");
  info.append("root_bucket_size:").append(std::to_string(bucket_count_)).append(",");
  info.append("overflow_bucket_size:").append(std::to_string(overflow_bucket_size_.readFull())).append(",");
  return info;
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
std::pair<uint32_t, uint8_t> CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::get_idx_and_tag(const key_type& key) const {
  auto hash = hasher()(key);
  hash ^= hash >> 32;
  hash *= 0x9E3779B97F4A7C15ULL;
  hash ^= hash >> 23;
  uint8_t tag = hash & bucket_type::kTagMask;
  if (tag >= bucket_type::kEmptyCtrl) {
    tag -= bucket_type::kCtrlNum;
  }
  uint32_t bucket_index = static_cast<uint32_t>((hash >> bucket_type::kTagMaskBits) % bucket_count_);
  return {bucket_index, tag};
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
bool CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::acquire_empty_slot(iterator& iter, uint8_t tag,
                                                                       node_type* new_node) {
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
    // bucket->update_counters(offset, get_timestamp(opts_.time_scale));
    bucket->touch_create(offset, opts_);
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
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename MatchFunc>
size_t CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::erase_slot(iterator& iter, const key_type& key, uint8_t tag,
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

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
bool CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::find_slot(iterator& iter, const K& key, uint8_t tag) const {
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
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename Filter>
bool CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::filter_find_slot(iterator& iter, const key_type& key, uint8_t tag,
                                                                     Filter&& filter) {
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
        if (!filter(protect_node->getItem().second)) {
          if (bucket->erase_slot(offset, protect_node, tag)) {
            dec_size(iter.get_bucket_idx());
          }
          return false;
        }
        iter.set_node(bucket, offset, protect_node);
        return true;
      }
    }
  }
  if (bucket->overflow.load()) {
    iter.set_node(bucket->get_overflow(), 0, nullptr);
    return filter_find_slot(iter, key, tag, std::move(filter));
  } else {
    return false;
  }
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename MatchFunc>
bool CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::do_insert(uint32_t bucket_idx, iterator& iter, InsertType type,
                                                              uint8_t tag, node_type* node, MatchFunc match) {
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
          // bucket->update_counters(iter.get_bucket_offset(), get_timestamp(opts_.time_scale));
          bucket->touch_write(iter.get_bucket_offset(), opts_);
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

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
void CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::try_evict(size_t bucket_idx) {
  auto& bucket_meta = bucket_metas_[bucket_idx];
  auto* bucket = buckets_ + bucket_idx;
  size_t retry = 0;
  bool evicting = false;
  if (!bucket_meta.evicting.compare_exchange_strong(evicting, true)) {
    return;
  }

  while (bucket_meta.size.load() >= (kBucketSlotSize - opts_.bucket_reserved_slots) && retry < kMaxEvictRetry) {
    retry++;
    ++evict_retry_counter_;
    // auto [evict_bucket, evict_offset, evict_node, evict_slot_tag] = find_oldest_slot(bucket_idx);
    auto [evict_bucket, evict_offset, evict_node, evict_slot_tag] = bucket->find_victim(opts_);

    if (evict_bucket == nullptr) {
      continue;
    }
    if (evict_bucket->erase_slot(evict_offset, evict_node, evict_slot_tag)) {
      dec_size(bucket_idx);
    }
  }
  bucket_meta.evicting.store(false);
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::find(const K& key) const {
  auto [bucket_index, tag] = get_idx_and_tag(key);
  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
  if (find_slot(iter, key, tag)) {
    iter.get_bucket()->touch_read(iter.get_bucket_offset(), opts_);
    return iter;
  } else {
    return end();
  }
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename Filter>
typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::filter_find(const K& key, Filter&& filter) {
  auto [bucket_index, tag] = get_idx_and_tag(key);
  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
  if (filter_find_slot(iter, key, tag, std::move(filter))) {
    iter.get_bucket()->touch_read(iter.get_bucket_offset(), opts_);
    return iter;
  } else {
    return end();
  }
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::begin() const noexcept {
  return iterator(buckets_, bucket_count_);
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::end() const noexcept {
  return iterator();
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::cbegin() const noexcept {
  return iterator(buckets_, bucket_count_);
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::cend() const noexcept {
  return iterator();
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::erase(const key_type& key) {
  auto [bucket_index, tag] = get_idx_and_tag(key);
  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
  auto lock = lock_bucket(bucket_index);
  return erase_slot(iter, key, tag, [](const value_type&) { return true; });
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename... Args>
std::pair<typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator, bool>
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::emplace(Args&&... args) {
  auto* node = create<node_type>(cohort(), std::forward<Args>(args)...);
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);

  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
  try_evict(bucket_index);
  auto success = do_insert(bucket_index, iter, InsertType::DOES_NOT_EXIST, tag, node, [](const V&) { return false; });

  if (!success) {
    destroy(node);
    return {std::move(iter), false};
  }
  return {std::move(iter), true};
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename... Args>
bool CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::insert_or_assign(Args&&... args) {
  auto* node = create<node_type>(cohort(), std::forward<Args>(args)...);
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);
  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
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
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename... Args>
std::optional<typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator>
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::assign(Args&&... args) {
  auto* node = create<node_type>(cohort(), std::forward<Args>(args)...);
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);
  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
  auto success = do_insert(bucket_index, iter, InsertType::MUST_EXIST, tag, node, [](const V&) { return false; });
  if (!success) {
    destroy(node);
    return {};
  }
  return iter;
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename Key, typename Value, typename Predicate>
std::optional<typename CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::const_iterator>
CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::assign_if(Key&& k, Value&& desired, Predicate&& predicate) {
  auto* node = create<node_type>(cohort(), std::move(k), std::move(desired));
  auto [bucket_index, tag] = get_idx_and_tag(node->getItem().first);
  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
  auto success = do_insert(bucket_index, iter, InsertType::MATCH, tag, node, std::forward<Predicate>(predicate));
  if (!success) {
    destroy(node);
    return {};
  }
  return iter;
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
template <typename Predicate>
size_t CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::erase_key_if(const key_type& key, Predicate&& predicate) {
  auto [bucket_index, tag] = get_idx_and_tag(key);
  iterator iter(buckets_, bucket_count_, bucket_index, buckets_ + bucket_index, 0);
  return erase_slot(iter, key, tag, std::forward<Predicate>(predicate));
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::bucket_count() const {
  return bucket_count_;
}

template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::size() const {
  return static_cast<size_t>(size_.readFull());
}
template <class K, class V, class Hash, class Eq, class Alloc,
          template <typename, typename, typename, template <typename> class> class CacheBucket>
size_t CacheImpl<K, V, Hash, Eq, Alloc, CacheBucket>::capacity() const {
  return static_cast<size_t>(capacity_.readFull());
}

}  // namespace detail

}  // namespace concurrent_cache
