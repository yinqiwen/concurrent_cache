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
#include <memory>
#include <vector>
#include "folly/synchronization/MicroSpinLock.h"
namespace concurrent_cache {

class MicroLocks {
 public:
  struct Lock {
    virtual void lock() const = 0;
    virtual void unlock() const = 0;
    virtual bool try_lock() const = 0;
    virtual ~Lock() {}
  };

  explicit MicroLocks(size_t capacity);

  class Guard {
   public:
    explicit Guard(MicroLocks* locks, uint64_t id) : locks_(locks), id_(id) { locks_->lock(id_); }
    ~Guard() { locks_->unlock(id_); }

   private:
    MicroLocks* locks_;
    uint64_t id_;
  };

  void lock(uint64_t id);
  bool try_lock(uint64_t id);
  void unlock(uint64_t id);

  std::unique_ptr<Guard> get_guard(uint64_t id);

 private:
  folly::MicroSpinLock* get_lock(uint64_t id);
  std::vector<folly::MicroSpinLock> locks_;
  size_t bucket_count_;
};

}  // namespace concurrent_cache