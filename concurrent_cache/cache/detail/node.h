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
  ~NodeT() {
    // printf("~NodeT\n");
  }

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
    // auto* new_node = clone_node(p);
    if (slots[i].compare_exchange_strong(expect, p)) {
      // node_pool.destroy(p);
      return true;
    }
    // node_pool.destroy(new_node);
    return false;
  }
  inline Node* get_slot(size_t i) { return slots[i].load(); }

  inline bool replace_slot(size_t i, Node* current, Node* new_node) {
    // auto* new_slot_node = clone_node(new_node);
    if (slots[i].compare_exchange_strong(current, new_node)) {
      // node_pool.destroy(new_node);
      return true;
    }
    // node_pool.destroy(new_slot_node);
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
      if (Parent::slots[i].load()) {
        FOLLY_SAFE_FATAL("init non empty");
      }
    }
  }

  inline bool erase_slot(size_t offset, typename Parent::Node* to_erase, uint8_t tag) {
    if (Parent::set_tag(offset, tag, Parent::kBusyCtrl)) {
      clear_access_timestamp(offset);
      if (Parent::destroy_slot(offset, to_erase, true)) {
        clear_access_timestamp(offset);
        if (!Parent::set_tag(offset, Parent::kBusyCtrl, Parent::kEmptyCtrl)) {
          FOLLY_SAFE_FATAL("set taf from kErasedCtrl to kEmptyCtrl");
        }
        // if (Parent::slots[offset].load()) {
        //   FOLLY_SAFE_FATAL("not null after reempty");
        // }
        return true;
      } else {
        FOLLY_SAFE_FATAL("destroy_slot failed");
      }
    }
    return false;
  }

  Self* get_overflow() { return reinterpret_cast<Self*>(Parent::overflow.load()); }
  inline void set_access_timestamp(size_t i, uint32_t ts) {
    if (Parent::valid(i)) {
      access_ts[i] = ts;
    }
  }
  inline void clear_access_timestamp(size_t i) { access_ts[i] = std::numeric_limits<uint32_t>::max(); }
};

}  // namespace detail
}  // namespace concurrent_cache