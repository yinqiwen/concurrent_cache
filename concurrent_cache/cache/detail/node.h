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

#include <folly/concurrency/ConcurrentHashMap.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "folly/ConcurrentBitSet.h"
#include "folly/lang/SafeAssert.h"
#include "folly/synchronization/Hazptr.h"

#include "concurrent_cache/cache/detail/hazptr.h"
#include "concurrent_cache/cache/options.h"
#include "concurrent_cache/common/time.h"
#include "concurrent_cache/simd/simd_ops.h"

namespace concurrent_cache {

namespace detail {

static constexpr size_t kBucketSlotSize = 64;

void record_alive_node_counter(bool inc_or_dec);
int64_t get_alive_node_counter();

// copy from https://github.com/facebook/folly/blob/main/folly/concurrency/detail/ConcurrentHashMap-detail.h
template <typename KeyType, typename ValueType, typename Allocator, template <typename> class Atom,
          typename Enabled = void>
class ValueHolder {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;

  explicit ValueHolder(const ValueHolder& other) : item_(other.item_) {}

  template <typename Arg, typename... Args>
  ValueHolder(std::piecewise_construct_t, Arg&& k, Args&&... args)
      : item_(std::piecewise_construct, std::forward_as_tuple(std::forward<Arg>(k)),
              std::forward_as_tuple(std::forward<Args>(args)...)) {}
  value_type& getItem() { return item_; }
  const value_type& getItem() const { return item_; }

 private:
  value_type item_;
};

// If the ValueType is not copy constructible, we can instead add
// an extra indirection.  Adds more allocations / deallocations and
// pulls in an extra cacheline.
template <typename KeyType, typename ValueType, typename Allocator, template <typename> class Atom>
class ValueHolder<KeyType, ValueType, Allocator, Atom,
                  std::enable_if_t<!std::is_nothrow_copy_constructible<ValueType>::value ||
                                   !std::is_nothrow_copy_constructible<KeyType>::value>> {
  typedef std::pair<const KeyType, ValueType> value_type;

  struct CountedItem {
    value_type kv_;
    Atom<uint32_t> numlinks_{1};  // Number of incoming links

    template <typename Arg, typename... Args>
    CountedItem(std::piecewise_construct_t, Arg&& k, Args&&... args)
        : kv_(std::piecewise_construct, std::forward_as_tuple(std::forward<Arg>(k)),
              std::forward_as_tuple(std::forward<Args>(args)...)) {}

    value_type& getItem() { return kv_; }

    void acquireLink() { uint32_t count = numlinks_.fetch_add(1, std::memory_order_release); }

    bool releaseLink() {
      uint32_t count = numlinks_.load(std::memory_order_acquire);

      if (count > 1) {
        count = numlinks_.fetch_sub(1, std::memory_order_acq_rel);
      }
      return count == 1;
    }
  };  // CountedItem
  // Back to ValueHolder specialization

  CountedItem* item_;  // Link to unique key-value item.

 public:
  explicit ValueHolder(const ValueHolder& other) {
    item_ = other.item_;
    item_->acquireLink();
  }

  ValueHolder& operator=(const ValueHolder&) = delete;

  template <typename Arg, typename... Args>
  ValueHolder(std::piecewise_construct_t, Arg&& k, Args&&... args) {
    item_ = (CountedItem*)Allocator().allocate(sizeof(CountedItem));
    new (item_) CountedItem(std::piecewise_construct, std::forward<Arg>(k), std::forward<Args>(args)...);
  }

  ~ValueHolder() {
    if (item_->releaseLink()) {
      item_->~CountedItem();
      Allocator().deallocate((uint8_t*)item_, sizeof(CountedItem));
    }
  }
  value_type& getItem() { return item_->getItem(); }
  const value_type& getItem() const { return item_->getItem(); }
};  // ValueHolder specialization

// hazptr deleter that can use an allocator.
template <typename Allocator>
class HazptrDeleter {
 public:
  HazptrDeleter() {}
  template <typename Node>
  void operator()(Node* node) {
    node->~Node();
    Allocator().deallocate((uint8_t*)node, sizeof(Node));
    record_alive_node_counter(false);
  }

 private:
};

class HazptrTableDeleter {
  size_t count_;

 public:
  HazptrTableDeleter(size_t count) : count_(count) {}
  HazptrTableDeleter() = default;
  template <typename Table>
  void operator()(Table* table) {
    table->destroy(count_);
  }
};

template <typename KeyType, typename ValueType, typename Allocator, template <typename> class Atom = std::atomic>
class NodeT
    : public folly::hazptr_obj_base<NodeT<KeyType, ValueType, Allocator, Atom>, Atom, HazptrDeleter<Allocator>> {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;
  using Self = NodeT<KeyType, ValueType, Allocator, Atom>;

  using Parent = folly::hazptr_obj_base<NodeT<KeyType, ValueType, Allocator, Atom>, Atom, HazptrDeleter<Allocator>>;

  explicit NodeT(folly::hazptr_obj_cohort<Atom>* cohort, NodeT* other) : item_(other->item_) { init(cohort); }

  template <typename Arg, typename... Args>
  NodeT(folly::hazptr_obj_cohort<Atom>* cohort, Arg&& k, Args&&... args)
      : item_(std::piecewise_construct, std::forward<Arg>(k), std::forward<Args>(args)...) {
    init(cohort);
  }
  ~NodeT() {}

  void retire() { Parent::retire(HazptrDeleter<Allocator>()); }

  value_type& getItem() { return item_.getItem(); }
  const value_type& getItem() const { return item_.getItem(); }

 private:
  void init(folly::hazptr_obj_cohort<Atom>* cohort) {
    // DCHECK(cohort);
    this->set_deleter(  // defined in hazptr_obj
        HazptrDeleter<Allocator>());
    if (cohort != nullptr) {
      this->set_cohort_tag(cohort);  // defined in hazptr_obj
    }
  }

  ValueHolder<KeyType, ValueType, Allocator, Atom> item_;
};

template <typename KeyType, typename ValueType, typename Allocator, template <typename> class Atom = std::atomic>
struct Bucket {
  static constexpr size_t kTagMask = 0xFF;
  static constexpr size_t kTagMaskBits = 8;

  static constexpr uint8_t kErasedCtrl = static_cast<uint8_t>(255);
  static constexpr uint8_t kBusyCtrl = static_cast<uint8_t>(254);
  static constexpr uint8_t kEmptyCtrl = static_cast<uint8_t>(253);
  using Node = NodeT<KeyType, ValueType, Allocator, Atom>;
  // using Slot = folly::hazptr_root<Node, Atom>;
  using Slot = Atom<Node*>;

  Atom<uint8_t> tags[kBucketSlotSize];
  Slot slots[kBucketSlotSize];
  Atom<Bucket*> overflow{nullptr};

  Bucket() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      tags[i] = kEmptyCtrl;
      slots[i] = nullptr;
    }
  }

  inline bool valid(uint32_t offset) const {
    if (offset >= kBucketSlotSize) {
      return false;
    }
    return tags[offset].load() < kEmptyCtrl;
  }

  inline bool acquire_empty(size_t offset) {
    while (1) {
      uint8_t expect = kEmptyCtrl;
      if (!tags[offset].compare_exchange_strong(expect, kBusyCtrl)) {
        if (expect == kBusyCtrl) {
          ::sched_yield();
          continue;
        } else {
          return false;
        }
      } else {
        return true;
      }
    }
  }
  inline uint8_t get_tag(size_t offset) const { return tags[offset].load(); }
  inline bool set_tag(size_t offset, uint8_t cmp_tag, uint8_t new_tag) {
    return tags[offset].compare_exchange_strong(cmp_tag, new_tag);
  }

  inline bool set_overflow(Bucket* p) {
    if (overflow) {
      return overflow.load()->set_overflow(p);
    }
    Bucket* expect = nullptr;
    return overflow.compare_exchange_strong(expect, p);
  }

  inline bool destroy_slot(size_t i, Node* to_erase, bool retire) {
    if (to_erase) {
      if (slots[i].compare_exchange_strong(to_erase, nullptr)) {
        if (retire) {
          to_erase->retire();
        }
        return true;
      }
    }
    return false;
  }

  inline bool erase_slot(size_t offset, Node* to_erase, uint8_t tag) {
    if (slots[offset].load() != to_erase) {
      return false;
    }
    if (set_tag(offset, tag, kBusyCtrl)) {
      if (destroy_slot(offset, to_erase, true)) {
        if (!set_tag(offset, kBusyCtrl, kEmptyCtrl)) {
          FOLLY_SAFE_FATAL("set taf from kErasedCtrl to kEmptyCtrl");
        }
        return true;
      } else {
        FOLLY_SAFE_FATAL("destroy_slot failed");
      }
    }
    return false;
  }

  inline bool set_slot(size_t i, Node* p) {
    Node* expect = nullptr;
    if (slots[i].compare_exchange_strong(expect, p)) {
      return true;
    }
    return false;
  }
  inline Node* get_slot(size_t i) { return slots[i].load(); }

  inline bool replace_slot(size_t i, Node* current, Node* new_node) {
    if (slots[i].compare_exchange_strong(current, new_node)) {
      return true;
    }
    return false;
  }

  void unlink_and_reclaim_nodes() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      destroy_slot(i, slots[i].load(std::memory_order_relaxed), true);
    }
  }

  ~Bucket() {
    unlink_and_reclaim_nodes();
    if (overflow) {
      delete overflow.load();
    }
  }
};
template <typename KeyType, typename ValueType, typename Allocator, template <typename> class Atom = std::atomic>
struct LRUBucket : public Bucket<KeyType, ValueType, Allocator, Atom> {
  using Self = LRUBucket<KeyType, ValueType, Allocator, Atom>;
  using Parent = Bucket<KeyType, ValueType, Allocator, Atom>;
  uint32_t access_ts[kBucketSlotSize];
  LRUBucket() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      access_ts[i] = std::numeric_limits<uint32_t>::max();
    }
  }
  inline void clear_counters(size_t i) { access_ts[i] = std::numeric_limits<uint32_t>::max(); }

  inline bool erase_slot(size_t offset, typename Parent::Node* to_erase, uint8_t tag) {
    if (Parent::destroy_slot(offset, to_erase, true)) {
      clear_counters(offset);
      if (!Parent::set_tag(offset, tag, Parent::kEmptyCtrl)) {
        FOLLY_SAFE_FATAL("set taf from kErasedCtrl to kEmptyCtrl");
      }
      return true;
    } else {
      return false;
    }
  }

  Self* get_overflow() { return reinterpret_cast<Self*>(Parent::overflow.load()); }

  inline std::tuple<Self*, uint32_t, typename Parent::Node*, uint8_t> find_victim(const CacheOptions& opts) {
    Self* evict_bucket = nullptr;
    Self* eval_bucket = this;
    uint32_t oldest_ts = 0;
    uint32_t evict_offset = 0;
    uint8_t evict_slot_tag = 0;
    typename Parent::Node* evict_node = nullptr;
    while (eval_bucket != nullptr) {
      auto [min_ts, offset] = simd::simd_vector_min(eval_bucket->access_ts, kBucketSlotSize);
      if (min_ts > 0) {
        if (eval_bucket->access_ts[offset] != min_ts) {
          // already updated by other thread, try again
          return {nullptr, 0, nullptr, 0};
        }
        auto tag = eval_bucket->get_tag(offset);
        if (tag >= Parent::kEmptyCtrl) {
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
  inline void touch(size_t idx, const CacheOptions& opts) {
    if (Parent::valid(idx)) {
      access_ts[idx] = get_timestamp(opts.time_scale);
    }
  }
  inline void touch_create(size_t idx, const CacheOptions& opts) { touch(idx, opts); }
  inline void touch_read(size_t idx, const CacheOptions& opts) { touch(idx, opts); }
  inline void touch_write(size_t idx, const CacheOptions& opts) { touch(idx, opts); }
};

template <typename KeyType, typename ValueType, typename Allocator, template <typename> class Atom = std::atomic>
struct FIFOBucket : public Bucket<KeyType, ValueType, Allocator, Atom> {
  using Self = FIFOBucket<KeyType, ValueType, Allocator, Atom>;
  using Parent = Bucket<KeyType, ValueType, Allocator, Atom>;
  uint32_t create_ts[kBucketSlotSize];
  FIFOBucket() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      create_ts[i] = std::numeric_limits<uint32_t>::max();
    }
  }
  inline void clear_counters(size_t i) { create_ts[i] = std::numeric_limits<uint32_t>::max(); }

  inline bool erase_slot(size_t offset, typename Parent::Node* to_erase, uint8_t tag) {
    if (Parent::destroy_slot(offset, to_erase, true)) {
      clear_counters(offset);
      if (!Parent::set_tag(offset, tag, Parent::kEmptyCtrl)) {
        FOLLY_SAFE_FATAL("set taf from kErasedCtrl to kEmptyCtrl");
      }
      return true;
    } else {
      return false;
    }
  }

  Self* get_overflow() { return reinterpret_cast<Self*>(Parent::overflow.load()); }

  inline std::tuple<Self*, uint32_t, typename Parent::Node*, uint8_t> find_victim(const CacheOptions& opts) {
    Self* evict_bucket = nullptr;
    Self* eval_bucket = this;
    uint32_t oldest_ts = 0;
    uint32_t evict_offset = 0;
    uint8_t evict_slot_tag = 0;
    typename Parent::Node* evict_node = nullptr;
    while (eval_bucket != nullptr) {
      auto [min_ts, offset] = simd::simd_vector_min(eval_bucket->create_ts, kBucketSlotSize);
      if (min_ts > 0) {
        if (eval_bucket->create_ts[offset] != min_ts) {
          // already updated by other thread, try again
          return {nullptr, 0, nullptr, 0};
        }
        auto tag = eval_bucket->get_tag(offset);
        if (tag >= Parent::kEmptyCtrl) {
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

  inline void touch_create(size_t idx, const CacheOptions& opts) {
    if (Parent::valid(idx)) {
      create_ts[idx] = get_timestamp(opts.time_scale);
    }
  }
  inline void touch_read(size_t idx, const CacheOptions& opts) {}
  inline void touch_write(size_t idx, const CacheOptions& opts) {}
};

#define LFU_INIT_VAL 2
#define LFU_MAX_VAL 254

// tiny LFU
template <typename KeyType, typename ValueType, typename Allocator, template <typename> class Atom = std::atomic>
struct LFUBucket : public Bucket<KeyType, ValueType, Allocator, Atom> {
  using Self = LFUBucket<KeyType, ValueType, Allocator, Atom>;
  using Parent = Bucket<KeyType, ValueType, Allocator, Atom>;
  // uint32_t access_ts[kBucketSlotSize];
  std::atomic<uint8_t> lfu_counter[kBucketSlotSize];
  std::atomic<uint32_t> access_counter{0};
  std::atomic<bool> decaying_lfu_counters{false};
  LFUBucket() {
    for (size_t i = 0; i < kBucketSlotSize; i++) {
      clear_counters(i);
    }
  }

  void try_decay_cache_lfu_counter(const CacheOptions& opts) {
    auto count = access_counter.fetch_add(1);
    if (count % opts.lfu_update_window_size == 0) {
      bool expect = false;
      if (decaying_lfu_counters.compare_exchange_strong(expect, true)) {
        simd::simd_decay_lfu_counters(reinterpret_cast<uint8_t*>(lfu_counter), kBucketSlotSize,
                                      reinterpret_cast<const uint8_t*>(Parent::tags), Parent::kEmptyCtrl);
        decaying_lfu_counters.store(false);
      }
    }
  }

  // uint32_t lfu_time_elapsed(uint32_t ldt, uint32_t now) {
  //   // unsigned long now = LFUGetTimeInMinutes();
  //   if (now >= ldt) return now - ldt;
  //   return std::numeric_limits<uint32_t>::max() - ldt + now;
  // }

  // uint8_t decr_get_lfu(size_t idx, uint32_t now, const CacheOptions& opts) {
  //   uint32_t ldt = access_ts[idx];
  //   unsigned long num_periods = opts.lfu_decay_time ? lfu_time_elapsed(ldt, now) / opts.lfu_decay_time : 0;
  //   uint8_t counter = lfu_counter[idx];
  //   if (num_periods) {
  //     counter = (num_periods > counter) ? 0 : counter - num_periods;
  //   }
  //   return counter;
  // }

  void incr_lfu(size_t offset, const CacheOptions& opts) {
    // slow
    // if (counter == LFU_MAX_VAL) return LFU_MAX_VAL;
    // double r = (double)rand() / RAND_MAX;
    // double baseval = counter - LFU_INIT_VAL;
    // if (baseval < 0) baseval = 0;
    // double p = 1.0 / (baseval * opts.lfu_log_factor + 1);
    // if (r < p) counter++;
    // return counter;
    // if (counter == LFU_MAX_VAL) return LFU_MAX_VAL;
    // return counter + 1;

    while (1) {
      auto current = lfu_counter[offset].load();
      if (current >= LFU_MAX_VAL) {
        break;
      }
      if (lfu_counter[offset].compare_exchange_strong(current, current + 1)) {
        break;
      }
    }
    try_decay_cache_lfu_counter(opts);
  }

  inline void clear_counters(size_t i) {
    // access_ts[i] = std::numeric_limits<uint32_t>::max();
    lfu_counter[i] = std::numeric_limits<uint8_t>::max();
  }

  inline bool erase_slot(size_t offset, typename Parent::Node* to_erase, uint8_t tag) {
    if (Parent::destroy_slot(offset, to_erase, true)) {
      clear_counters(offset);
      if (!Parent::set_tag(offset, tag, Parent::kEmptyCtrl)) {
        FOLLY_SAFE_FATAL("set taf from kErasedCtrl to kEmptyCtrl");
      }
      return true;
    } else {
      return false;
    }
  }

  Self* get_overflow() { return reinterpret_cast<Self*>(Parent::overflow.load()); }

  std::tuple<Self*, uint32_t, typename Parent::Node*, uint8_t> find_victim(const CacheOptions& opts) {
    uint32_t now = get_timestamp(opts.time_scale);
    uint8_t result_counters[kBucketSlotSize];

    Self* evict_bucket = nullptr;
    Self* eval_bucket = this;
    uint32_t min_lfu_counter = 0;
    uint32_t evict_offset = 0;
    uint8_t evict_slot_tag = 0;
    typename Parent::Node* evict_node = nullptr;

    while (eval_bucket != nullptr) {
      auto [min_counter, offset] =
          simd::simd_vector_min(reinterpret_cast<const uint8_t*>(eval_bucket->lfu_counter), kBucketSlotSize);
      auto tag = eval_bucket->get_tag(offset);
      if (tag >= Parent::kEmptyCtrl) {
        return {nullptr, 0, nullptr, 0};
      }
      if (evict_bucket == nullptr || min_counter < min_lfu_counter) {
        evict_offset = offset;
        evict_bucket = eval_bucket;
        min_lfu_counter = min_counter;
        evict_slot_tag = tag;
        evict_node = eval_bucket->slots[offset].load();
      }
      eval_bucket = eval_bucket->get_overflow();
    }
    return {evict_bucket, evict_offset, evict_node, evict_slot_tag};
  }
  inline void touch(size_t idx, const CacheOptions& opts) {
    if (Parent::valid(idx)) {
      // uint8_t counter = decr_get_lfu(idx, now, opts);
      // counter = incr_lfu(counter, opts);
      // lfu_counter[idx] = incr_lfu(lfu_counter[idx], opts);
      incr_lfu(idx, opts);
    }
  }
  inline void touch_create(size_t idx, const CacheOptions& opts) {
    lfu_counter[idx] = LFU_INIT_VAL;
    // access_ts[idx] = get_timestamp(opts.time_scale);
  }
  inline void touch_read(size_t idx, const CacheOptions& opts) { touch(idx, opts); }
  inline void touch_write(size_t idx, const CacheOptions& opts) { touch(idx, opts); }
};

}  // namespace detail
}  // namespace concurrent_cache