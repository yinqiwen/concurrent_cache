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
#include "concurrent_cache/common/lock.h"
#include <boost/preprocessor/repetition/repeat.hpp>
#include <memory>
#include "concurrent_cache/common/allign.h"
namespace concurrent_cache {

MicroLocks::MicroLocks(size_t capacity) {
  if (capacity <= 1) {
    capacity = 1024 * 1024;
  }
  bucket_count_ = next_prime(capacity);
  locks_.resize(bucket_count_);
}
void MicroLocks::lock(uint64_t id) {
  auto* p = get_lock(id);
  p->lock();
}
void MicroLocks::unlock(uint64_t id) {
  auto* p = get_lock(id);
  p->unlock();
}
bool MicroLocks::try_lock(uint64_t id) {
  auto* p = get_lock(id);
  return p->try_lock();
}

std::unique_ptr<typename MicroLocks::Guard> MicroLocks::get_guard(uint64_t id) {
  return std::make_unique<Guard>(this, id);
}

folly::MicroSpinLock* MicroLocks::get_lock(uint64_t hash) {
  hash ^= hash >> 32;
  hash *= 0x9E3779B97F4A7C15ULL;
  hash ^= hash >> 23;
  uint64_t idx = hash % bucket_count_;
  return &locks_[idx];
}

}  // namespace concurrent_cache