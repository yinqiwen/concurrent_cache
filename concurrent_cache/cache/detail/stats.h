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
#include <string>

namespace concurrent_cache {
struct CacheStats {
  size_t init_capacity = 0;
  size_t size = 0;
  size_t capacity = 0;
  size_t erasing = 0;
  size_t overflow_size = 0;

  std::string ToString() const {
    std::string str;
    str.append("init_capacity:").append(std::to_string(init_capacity)).append(",");
    str.append("capacity:").append(std::to_string(capacity)).append(",");
    str.append("size:").append(std::to_string(size)).append(",");
    str.append("erasing:").append(std::to_string(erasing)).append(",");
    str.append("overflow_size:").append(std::to_string(overflow_size));
    return str;
  }
};
}  // namespace concurrent_cache