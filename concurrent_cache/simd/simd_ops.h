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
#include <string.h>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "hwy/bit_set.h"
namespace concurrent_cache {
namespace simd {
uint64_t simd_vector_match(const uint8_t* data, size_t len, uint8_t v);

std::pair<uint32_t, uint16_t> simd_vector_min(const uint32_t* data, size_t len);

class MaskIterator {
 public:
  inline MaskIterator() noexcept = default;

  inline explicit MaskIterator(uint64_t mask) noexcept { memcpy(&bitset_, &mask, sizeof(uint64_t)); }

  explicit operator bool() const noexcept { return bitset_.Any(); }

  inline size_t Advance() noexcept {
    size_t n = bitset_.First();
    bitset_.Clear(n);
    return n;
  }

 private:
  hwy::BitSet64 bitset_;
};
}  // namespace simd
}  // namespace concurrent_cache