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
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <random>
#include <thread>
#include "concurrent_cache/cache/ttl_cache.h"

#include "folly/synchronization/PicoSpinLock.h"

// TEST(HashMap, simple) {
//   concurrent_cache::LRUCache<int64_t, int64_t> cache;

//   for (int64_t i = 0; i < 10000; i++) {
//     auto res = cache.emplace(i, i + 1);
//     ASSERT_TRUE(res.second);
//   }

//   for (int64_t i = 0; i < 10000; i++) {
//     auto res = cache.find(i);
//     ASSERT_TRUE(res != cache.end());
//   }

//   ASSERT_FALSE(cache.emplace(10, 11).second);
//   ASSERT_TRUE(cache.insert_or_assign(10, 11));   // overwrite exist
//   ASSERT_TRUE(cache.insert_or_assign(101, 11));  // insert new

//   ASSERT_TRUE(cache.assign(101, 12).has_value());  // assign exist success
//   // ASSERT_FALSE(cache.assign(102, 12).has_value());  // assign noneixst fail
// }

// TEST(HashMap, assign_erase_if) {
//   concurrent_cache::LRUCache<int64_t, int64_t> cache;

//   for (int64_t i = 0; i < 100; i++) {
//     auto res = cache.emplace(i, i + 1);
//     ASSERT_TRUE(res.second);
//   }

//   ASSERT_TRUE(cache.assign_if_equal(10, 11, 12).has_value());
//   ASSERT_EQ(cache.erase_if_equal(10, 11), 0);
//   ASSERT_EQ(cache.erase_if_equal(10, 12), 1);
// }
TEST(LFUCache, simple) {
  concurrent_cache::CacheOptions opts;
  opts.max_size = 1000;
  concurrent_cache::LFUCache<int64_t, int64_t> cache(opts);

  for (int64_t i = 0; i < 1000; i++) {
    auto res = cache.insert(i, i + 1);
    ASSERT_TRUE(res.second);
  }
  for (int64_t i = 0; i < 500; i++) {
    cache.find(i);
  }
  for (int64_t i = 1100; i < 1200; i++) {
    auto res = cache.insert(i, i + 1);
    ASSERT_TRUE(res.second);
  }
}

// TEST(TTLCache, simple) {
//   concurrent_cache::LRUTTLCache<int64_t, int64_t> cache;

//   for (int64_t i = 0; i < 100; i++) {
//     auto res = cache.insert(i, i + 1, std::chrono::seconds(1));
//     ASSERT_TRUE(res.second);
//   }
//   for (int64_t i = 0; i < 100; i++) {
//     auto found = cache.find(i);
//     ASSERT_EQ(found->second.value(), i + 1);
//     ASSERT_GT(found->second.pttl(), 0);
//   }
//   std::this_thread::sleep_for(std::chrono::seconds(2));
//   for (int64_t i = 0; i < 100; i++) {
//     auto found = cache.find(i);
//     ASSERT_EQ(found, cache.end());
//   }

//   concurrent_cache::LRUTTLCache<int64_t, std::string> cache1;
//   cache1.insert(1, "aaa", std::chrono::seconds(1));
//   ASSERT_TRUE(cache1.insert_or_assign(2, "bbb", std::chrono::seconds(1)));
//   auto ret = cache1.assign(3, "ccc", std::chrono::seconds(1));
//   ASSERT_FALSE(ret.has_value());
//   ret = cache1.assign(2, "ccc", std::chrono::seconds(1));
//   ASSERT_TRUE(ret.has_value());

//   ret = cache1.assign_if_equal(2, "ccc", "ddd", std::chrono::seconds(1));
//   ASSERT_TRUE(ret.has_value());
//   ASSERT_EQ(ret.value()->second.value(), "ddd");
// }
