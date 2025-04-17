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
#include "concurrent_cache/cache/detail/hazptr.h"
#include <folly/Singleton.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

namespace {
struct HazptrTPETag {};
folly::Singleton<folly::CPUThreadPoolExecutor, HazptrTPETag> hazptr_tpe_([] {
  return new folly::CPUThreadPoolExecutor(std::make_pair(4, 4),
                                          std::make_shared<folly::NamedThreadFactory>("cache_hazptr-tpe-"));
});

folly::Executor::KeepAlive<> get_hazptr_tpe() {
  auto ex = hazptr_tpe_.try_get();
  return ex ? ex.get() : nullptr;
}

}  // namespace

namespace concurrent_cache {
namespace detail {
// folly::hazptr_domain<std::atomic>& get_cache_hazptr_domain() {
//   static folly::hazptr_domain<std::atomic> domain;
//   return domain;
// }

void enable_cache_hazptr_thread_pool_executor() { folly::default_hazptr_domain().set_executor(&get_hazptr_tpe); }
}  // namespace detail
}  // namespace concurrent_cache