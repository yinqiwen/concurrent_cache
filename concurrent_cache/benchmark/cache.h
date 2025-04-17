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
#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "concurrent_cache/cache/hashmap.h"
#include "concurrent_cache/cache/lru_cache.h"
#include "folly/concurrency/ConcurrentHashMap.h"
#include "folly/container/EvictingCacheMap.h"

namespace concurrent_cache {
namespace benchmark {
struct TestCache {
  virtual int Init(size_t cache_size) = 0;
  virtual bool Put(uint64_t key, std::string&& val) = 0;
  virtual bool Get(uint64_t key, std::function<void(const std::string&)>&& value_cb) = 0;
  virtual std::string GetStats() = 0;
  virtual ~TestCache() {}
};

class HashMap : public TestCache {
 private:
  using Cache = concurrent_cache::ConcurrentFixedHashMap<uint64_t, std::string,
                                                         absl::container_internal::hash_default_hash<uint64_t>>;
  int Init(size_t cache_size) {
    cache_ = std::make_unique<Cache>(cache_size);
    return 0;
  }
  bool Put(uint64_t key, std::string&& val) { return cache_->insert_or_assign(key, std::move(val)); }
  bool Get(uint64_t key, std::function<void(const std::string&)>&& value_cb) {
    auto found = cache_->find(key);
    return found != cache_->end();
  }
  std::string GetStats() { return ""; }

  std::unique_ptr<Cache> cache_;
};

class FollyHashMap : public TestCache {
 private:
  using Cache = folly::ConcurrentHashMap<uint64_t, std::string, absl::container_internal::hash_default_hash<uint64_t>>;
  int Init(size_t cache_size) {
    cache_ = std::make_unique<Cache>(cache_size);
    return 0;
  }
  bool Put(uint64_t key, std::string&& val) { return cache_->insert_or_assign(key, std::move(val)).second; }
  bool Get(uint64_t key, std::function<void(const std::string&)>&& value_cb) {
    auto found = cache_->find(key);
    return found != cache_->end();
  }
  std::string GetStats() { return ""; }
  std::unique_ptr<Cache> cache_;
};

class FollyLRU : public TestCache {
 private:
  using Cache = folly::EvictingCacheMap<uint64_t, std::string>;
  int Init(size_t cache_size) {
    cache_ = std::make_unique<Cache>(cache_size);
    return 0;
  }
  bool Put(uint64_t key, std::string&& val) {
    std::unique_lock lock(mutex_);
    cache_->set(key, std::move(val));
    return true;
  }
  bool Get(uint64_t key, std::function<void(const std::string&)>&& value_cb) {
    std::unique_lock lock(mutex_);
    auto found = cache_->find(key);
    return found != cache_->end();
  }
  std::string GetStats() { return ""; }
  std::unique_ptr<Cache> cache_;
  std::mutex mutex_;
};

class LRU : public TestCache {
 private:
  int Init(size_t cache_size) {
    concurrent_cache::LRUCacheOptions opts;
    opts.max_size = cache_size;
    cache_ = std::make_unique<concurrent_cache::LRUCache<uint64_t, std::string>>(opts);
    return 0;
  }
  bool Put(uint64_t key, std::string&& val) { return cache_->insert_or_assign(key, std::move(val)); }
  bool Get(uint64_t key, std::function<void(const std::string&)>&& value_cb) {
    auto found = cache_->find(key);
    return found != cache_->end();
  }
  std::string GetStats() { return cache_->stats(); }

  std::unique_ptr<concurrent_cache::LRUCache<uint64_t, std::string>> cache_;
};

}  // namespace benchmark
}  // namespace concurrent_cache