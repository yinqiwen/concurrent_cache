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

#include "concurrent_cache/common/time.h"

namespace concurrent_cache {
struct CacheOptions {
  size_t max_size = 1024 * 1024;
  Timescale time_scale = Timescale::MILLISECOND;
  size_t bucket_reserved_slots = 4;
  int lfu_log_factor = 10;
  int lfu_decay_time = 1;
};
}  // namespace concurrent_cache