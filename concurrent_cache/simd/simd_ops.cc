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
#include "concurrent_cache/simd/simd_ops.h"

#include <limits>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "concurrent_cache/simd/simd_ops.cc"  // this file

#include "hwy/foreach_target.h"  // must come before highway.h

#include "hwy/contrib/algo/find-inl.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace concurrent_cache {
namespace simd {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

template <typename D>
HWY_INLINE void store_mask_bits(hn::Mask<D> mask, uint8_t* bits, size_t idx) {
  constexpr D d;
  constexpr size_t N = hn::Lanes(d);
  size_t bits_offset = idx / 8;
  size_t bits_cursor = idx % 8;

  if constexpr (N < 8) {
    uint8_t tmp[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    hn::StoreMaskBits(d, mask, tmp);
    bits[bits_offset] = (bits[bits_offset] | (tmp[0] << bits_cursor));
  } else {
    hn::StoreMaskBits(d, mask, bits + bits_offset);
  }
}

HWY_INLINE void simd_vector_match_impl(const uint8_t* data, size_t len, uint8_t cmp, uint64_t& mask_bits) {
  size_t max_match_len = len;
  if (max_match_len > sizeof(uint64_t) * 8) {
    max_match_len = sizeof(uint64_t) * 8;
  }
  uint64_t mask_bits_tmp[2] = {0, 0};
  uint8_t* mask_bits_p = reinterpret_cast<uint8_t*>(mask_bits_tmp);
  using D = hn::ScalableTag<uint8_t>;
  constexpr D d;
  constexpr size_t N = hn::Lanes(d);
  const hn::Vec<D> cmp_v = hn::Set(d, cmp);
  size_t idx = 0;
  if (len >= N) {
    for (; idx <= max_match_len - N; idx += N) {
      const hn::Vec<D> v = hn::LoadU(d, data + idx);
      hn::Mask<D> mask = hn::Eq(v, cmp_v);
      store_mask_bits<D>(mask, mask_bits_p, idx);
    }
  }
  if (HWY_UNLIKELY(idx == max_match_len)) {
    mask_bits = mask_bits_tmp[0];
    return;
  }
  const size_t remaining = max_match_len - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const hn::Vec<D> v = hn::LoadN(d, data + idx, remaining);
  hn::Mask<D> mask = hn::Eq(v, cmp_v);
  store_mask_bits<D>(mask, mask_bits_p, idx);
  mask_bits = mask_bits_tmp[0];
}

template <typename T>
HWY_INLINE T reduce_min(const T* data, size_t len) {
  T min_val = std::numeric_limits<T>::max();
  const hn::ScalableTag<T> d;
  constexpr auto lanes = hn::Lanes(d);
  size_t i = 0;
  for (; (i + lanes) <= len; i += lanes) {
    auto lv = hn::LoadU(d, data + i);
    auto min_v = hn::ReduceMin(d, lv);
    if (min_v < min_val) {
      min_val = min_v;
    }
  }
  if (HWY_UNLIKELY(i < len)) {
    for (; i < len; i++) {
      if (data[i] < min_val) {
        min_val = data[i];
      }
    }
  }
  return min_val;
}

template <typename T>
HWY_INLINE std::pair<T, uint16_t> simd_vector_min_impl(const T* data, size_t len) {
  uint32_t min_val = reduce_min(data, len);
  if (min_val == std::numeric_limits<T>::max()) {
    return {0, 0};
  }
  using D = hn::ScalableTag<T>;
  const D d;
  constexpr auto lanes = hn::Lanes(d);
  const hn::Vec<D> cmp = hn::Set(d, min_val);
  size_t idx = 0;
  for (; (idx + lanes) <= len; idx += lanes) {
    const hn::Vec<D> v = hn::LoadU(d, data + idx);
    auto mask = hn::Eq(v, cmp);
    auto found = hn::FindFirstTrue(d, mask);
    if (found >= 0) {
      return {min_val, found + idx};
    }
  }
  if (HWY_UNLIKELY(idx == len)) return {min_val, 0};
  const size_t remaining = len - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const hn::Vec<D> v = hn::LoadN(d, data + idx, remaining);
  auto mask = hn::Eq(v, cmp);
  auto found = hn::FindFirstTrue(d, mask);
  if (found >= 0 && static_cast<size_t>(found) < remaining) {
    return {min_val, found + idx};
  }
  return {min_val, 0};
}

HWY_INLINE void simd_decr_lfu_counters_impl(const uint32_t* ts, size_t len, uint32_t now, uint32_t decay,
                                            const uint8_t* counters, uint8_t* result_counters) {
  using D = hn::ScalableTag<uint32_t>;
  const hn::Rebind<uint8_t, D> du8;
  const D d;
  constexpr auto lanes = hn::Lanes(d);
  uint32_t max_ts = std::numeric_limits<uint32_t>::max();
  const hn::Vec<D> max_ts_val = hn::Set(d, max_ts);
  const hn::Vec<D> now_val = hn::Set(d, now);
  const hn::Vec<D> decay_val = hn::Set(d, decay);
  auto zero_u8 = hn::Zero(du8);
  auto max_counter_v = hn::Set(du8, std::numeric_limits<int8_t>::max());
  size_t idx = 0;
  for (; (idx + lanes) <= len; idx += lanes) {
    auto ts_v = hn::LoadU(d, ts + idx);
    auto counter_v = hn::LoadU(du8, counters + idx);
    auto filter = hn::Lt(ts_v, max_ts_val);
    auto elased = hn::Sub(now_val, ts_v);
    auto period = hn::Div(elased, decay_val);
    auto period_u8 = hn::U8FromU32(period);
    auto mask = hn::Gt(period_u8, counter_v);
    auto final_v = hn::IfThenZeroElse(mask, hn::Sub(counter_v, period_u8));
    final_v = hn::IfThenElse(hn::DemoteMaskTo(du8, d, filter), max_counter_v, final_v);
    hn::StoreU(final_v, du8, result_counters + idx);
  }
}

}  // namespace HWY_NAMESPACE
}  // namespace simd
}  // namespace concurrent_cache
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace concurrent_cache {
namespace simd {
uint64_t simd_vector_match(const uint8_t* data, size_t len, uint8_t v) {
  uint64_t mask = 0;
  HWY_EXPORT_T(Table, simd_vector_match_impl);
  HWY_DYNAMIC_DISPATCH_T(Table)(data, len, v, mask);
  return mask;
}

std::pair<uint32_t, uint16_t> simd_vector_min(const uint32_t* data, size_t len) {
  HWY_EXPORT_T(Table, simd_vector_min_impl<uint32_t>);
  return HWY_DYNAMIC_DISPATCH_T(Table)(data, len);
}
std::pair<uint8_t, uint16_t> simd_vector_min(const uint8_t* data, size_t len) {
  HWY_EXPORT_T(Table, simd_vector_min_impl<uint8_t>);
  return HWY_DYNAMIC_DISPATCH_T(Table)(data, len);
}

void simd_decr_lfu_counters(const uint32_t* ts, size_t len, uint32_t now, uint32_t decay, const uint8_t* counters,
                            uint8_t* result_counters) {
  HWY_EXPORT_T(Table, simd_decr_lfu_counters_impl);
  return HWY_DYNAMIC_DISPATCH_T(Table)(ts, len, now, decay, counters, result_counters);
}

}  // namespace simd
}  // namespace concurrent_cache

#endif  // HWY_ONCE