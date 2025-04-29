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

#include <folly/synchronization/HazptrHolder.h>
#include <atomic>
#include <cstdint>
#include <limits>
#include <utility>

#include "concurrent_cache/cache/detail/node.h"
#include "folly/synchronization/Hazptr.h"
#include "node.h"

namespace concurrent_cache {

namespace detail {

template <typename Map>
class Iterator {
 public:
  using bucket_type = typename Map::bucket_type;
  using value_type = typename Map::value_type;
  using key_type = typename Map::key_type;
  using mapped_type = typename Map::mapped_type;
  using allocator_type = typename Map::allocator_type;
  friend Map;
  static constexpr uint32_t kBucketIdxLimit = std::numeric_limits<uint32_t>::max();

  const value_type& operator*() const { return node_->getItem(); }

  const value_type* operator->() const { return &(node_->getItem()); }

  Iterator& operator++() {
    next();
    return *this;
  }

  Iterator& operator++(int) {
    next();
    return *this;
  }

  bool operator==(const Iterator& o) const {
    return root_buckets_ == o.root_buckets_ && root_bucket_idx_ == o.root_bucket_idx_ &&
           bucket_offset_ == o.bucket_offset_ && bucket_ == o.bucket_;
  }

  bool operator!=(const Iterator& o) const { return !(*this == o); }

  Iterator& operator=(const Iterator& o) = delete;

  Iterator& operator=(Iterator&& o) noexcept {
    if (this != &o) {
      hazptr_ = std::move(o.hazptr_);
      node_ = std::exchange(o.node_, nullptr);
      root_buckets_ = std::exchange(o.root_buckets_, nullptr);
      root_bucket_idx_ = std::exchange(o.root_bucket_idx_, kBucketIdxLimit);
      root_bucket_count_ = std::exchange(o.root_bucket_count_, 0);
      bucket_ = std::exchange(o.bucket_, nullptr);
      bucket_offset_ = std::exchange(o.bucket_offset_, 0);
    }
    return *this;
  }

  Iterator(const Iterator& o) = delete;

  Iterator(Iterator&& o) noexcept
      : hazptr_(std::move(o.hazptr_)),
        node_(std::exchange(o.node_, nullptr)),
        root_buckets_(std::exchange(o.root_buckets_, nullptr)),
        root_bucket_count_(std::exchange(o.root_bucket_count_, 0)),
        root_bucket_idx_(std::exchange(o.root_bucket_idx_, kBucketIdxLimit)),
        bucket_(std::exchange(o.bucket_, nullptr)),
        bucket_offset_(std::exchange(o.bucket_offset_, 0)) {}

  Iterator(const bucket_type* buckets, uint32_t bucket_count, uint32_t bucket_idx, bucket_type* bucket,
           uint32_t bucket_offset)
      : hazptr_(folly::make_hazard_pointer()),
        node_(nullptr),
        root_buckets_(buckets),
        root_bucket_count_(bucket_count),
        root_bucket_idx_(bucket_idx),
        bucket_(bucket),
        bucket_offset_(bucket_offset) {}

 private:
  using Node = NodeT<key_type, mapped_type, allocator_type>;
  // cbegin iterator
  explicit Iterator(const bucket_type* buckets, uint32_t bucket_count)
      : hazptr_(folly::make_hazard_pointer()),
        node_(nullptr),
        root_buckets_(buckets),
        root_bucket_count_(bucket_count),
        root_bucket_idx_(0),
        bucket_(const_cast<bucket_type*>(buckets)),
        bucket_offset_(0) {
    node_ = hazptr_.protect(bucket_->slots[0]);
    if (!bucket_->valid(0) || !node_) {
      next();
    }
  }

  // cend iterator
  explicit Iterator()
      : node_(nullptr),
        root_buckets_(nullptr),
        root_bucket_count_(0),
        root_bucket_idx_(0),
        bucket_(nullptr),
        bucket_offset_(0) {}

  inline bucket_type* get_bucket() { return bucket_; }
  inline uint32_t get_bucket_idx() const { return root_bucket_idx_; }
  inline uint32_t get_bucket_offset() const { return bucket_offset_; }
  inline Node* get_node() { return node_; }

  inline void set_node(bucket_type* bucket, uint32_t bucket_offset, Node* node) {
    bucket_ = bucket;
    bucket_offset_ = bucket_offset;
    node_ = node;
  }
  inline void set_node(uint32_t bucket_offset, Node* node) {
    bucket_offset_ = bucket_offset;
    node_ = node;
  }
  inline void set_node(Node* node) { node_ = node; }

  void next() {
    while (bucket_ != nullptr) {
      bucket_offset_++;
      if (bucket_offset_ >= kBucketSlotSize) {
        bucket_ = bucket_->get_overflow();
        bucket_offset_ = 0;
      }
      if (!bucket_) {
        if (!next_bucket()) {
          return;
        }
      }
      node_ = hazptr_.protect(bucket_->slots[bucket_offset_]);
      if (bucket_->valid(bucket_offset_) && node_) {
        return;
      }
    }
  }
  bool next_bucket() {
    if (root_buckets_ == nullptr || root_bucket_idx_ >= root_bucket_count_) {
      return false;
    }
    root_bucket_idx_++;
    if (root_bucket_idx_ >= root_bucket_count_) {
      root_buckets_ = nullptr;
      root_bucket_count_ = 0;
      root_bucket_idx_ = 0;
      bucket_ = nullptr;
      bucket_offset_ = 0;
      return false;
    }

    bucket_ = const_cast<bucket_type*>(root_buckets_ + root_bucket_idx_);
    bucket_offset_ = 0;
    return true;
  }

  folly::hazptr_holder<std::atomic> hazptr_;
  // folly::hazptr_array<2, std::atomic> hazptrs_;
  Node* node_;
  // const Map* parent_;
  const bucket_type* root_buckets_ = nullptr;
  uint32_t root_bucket_count_;
  uint32_t root_bucket_idx_;

  bucket_type* bucket_ = nullptr;
  uint32_t bucket_offset_;
};
}  // namespace detail
}  // namespace concurrent_cache