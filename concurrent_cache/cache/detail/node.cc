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
#include "concurrent_cache/cache/detail/node.h"
#include "folly/ThreadCachedInt.h"
namespace concurrent_cache {
namespace detail {
static folly::ThreadCachedInt<int64_t> g_alive_node_counter{0};
void record_alive_node_counter(bool inc_or_dec) {
  if (inc_or_dec) {
    ++g_alive_node_counter;
  } else {
    --g_alive_node_counter;
  }
}
int64_t get_alive_node_counter() { return g_alive_node_counter.readFull(); }
}  // namespace detail
}  // namespace concurrent_cache