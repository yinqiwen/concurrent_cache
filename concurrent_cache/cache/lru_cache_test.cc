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
#include "lru_cache.h"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <random>
#include <thread>

#include "folly/synchronization/PicoSpinLock.h"

TEST(HashMap, simple) {
  concurrent_cache::LRUCache<int64_t, int64_t> cache;

  for (int64_t i = 0; i < 10000; i++) {
    auto res = cache.emplace(i, i + 1);
    ASSERT_TRUE(res.second);
  }

  for (int64_t i = 0; i < 10000; i++) {
    auto res = cache.find(i);
    ASSERT_TRUE(res != cache.end());
  }

  ASSERT_FALSE(cache.emplace(10, 11).second);
  ASSERT_TRUE(cache.insert_or_assign(10, 11));   // overwrite exist
  ASSERT_TRUE(cache.insert_or_assign(101, 11));  // insert new

  ASSERT_TRUE(cache.assign(101, 12).has_value());  // assign exist success
  // ASSERT_FALSE(cache.assign(102, 12).has_value());  // assign noneixst fail
}

TEST(HashMap, assign_erase_if) {
  concurrent_cache::LRUCache<int64_t, int64_t> cache;

  for (int64_t i = 0; i < 100; i++) {
    auto res = cache.emplace(i, i + 1);
    ASSERT_TRUE(res.second);
  }

  ASSERT_TRUE(cache.assign_if_equal(10, 11, 12).has_value());
  ASSERT_EQ(cache.erase_if_equal(10, 11), 0);
  ASSERT_EQ(cache.erase_if_equal(10, 12), 1);
}
