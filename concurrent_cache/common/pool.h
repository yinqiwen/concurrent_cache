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
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "folly/MPMCQueue.h"

namespace concurrent_cache {

template <typename T, typename Allocator>
class Pool {
 private:
  // using Queue = moodycamel::ConcurrentQueue<T*>;
  using Queue = folly::MPMCQueue<T*>;

 public:
  static constexpr size_t kPoolWorkerNum = 128;
  explicit Pool(size_t capacity) {
    pool_worker_num_ = kPoolWorkerNum;
    size_t capacity_per_worker = capacity / kPoolWorkerNum;
    if (capacity_per_worker == 0) {
      capacity_per_worker = 1;
    }
    for (size_t i = 0; i < pool_worker_num_; i++) {
      shard_pools_.emplace_back(std::make_unique<Queue>(capacity_per_worker));
    }
    // printf("######capacity_per_worker:%d\n", capacity_per_worker);
  }
  template <typename... Args>
  T* Allocate(Args&&... args) {
    static __thread uint32_t cursor = 0;
    uint32_t pool_id = cursor % pool_worker_num_;
    cursor++;
    T* p = nullptr;
    if (shard_pools_[pool_id]->read(p) && p != nullptr) {
      // do nothing

    } else {
      uint8_t* memory = reinterpret_cast<uint8_t*>(Allocator().allocate(sizeof(T) + sizeof(uint64_t)));
      uint64_t id = pool_id;
      memcpy(memory, &id, sizeof(uint64_t));
      p = reinterpret_cast<T*>(memory + sizeof(uint64_t));
    }
    new (p) T(std::forward<Args>(args)...);
    return p;
  }
  void Recycle(T* p) { Destory(p); }

  ~Pool() {
    T* p = nullptr;
    for (auto& pool : shard_pools_) {
      while (pool->read(p)) {
        Deallocate(p);
      }
    }
  }

 private:
  void Deallocate(T* p) {
    uint8_t* memory = reinterpret_cast<uint8_t*>(p);
    Allocator().deallocate(memory - sizeof(uint64_t), sizeof(T) + sizeof(uint64_t));
  }
  void Destory(T* p) {
    p->~T();
    uint8_t* memory = reinterpret_cast<uint8_t*>(p);
    uint64_t pool_id = 0;
    memcpy(&pool_id, memory - sizeof(uint64_t), sizeof(uint64_t));
    if (!shard_pools_[pool_id]->write(p)) {
      Deallocate(p);
    } else {
    }
  }

  std::vector<std::unique_ptr<Queue>> shard_pools_;
  size_t pool_worker_num_;

  // std::unique_ptr<Queue> queue_;
};

}  // namespace concurrent_cache