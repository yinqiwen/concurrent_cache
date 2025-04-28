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
#include <sys/time.h>
#include <chrono>
#include <cstdint>
#include <utility>
namespace concurrent_cache {

static inline int64_t gettimeofday_us() {
  struct timeval tv;
  uint64_t ust;
  gettimeofday(&tv, nullptr);
  ust = ((int64_t)tv.tv_sec) * 1000000;
  ust += tv.tv_usec;
  return ust;
}
static const auto g_app_start_time = gettimeofday_us();

enum class Timescale {
  SECOND,
  MILLISECOND,
  MICROSECOND,
};
void enable_estimate_timestamp_updater();
uint64_t get_timestamp(Timescale scale);

template <typename T>
struct ValueWithTTL {
  T val;
  uint64_t expire_at_ms;

  ValueWithTTL() {}
  template <typename V>
  explicit ValueWithTTL(V&& v, uint64_t ttl) : val(std::forward<V>(v)), expire_at_ms(ttl) {}

  const T& value() const { return val; }
  uint64_t ttl() const { return pttl() / 1000; }
  uint64_t pttl() const {
    auto now_ms = get_timestamp(Timescale::MILLISECOND);
    if (now_ms < expire_at_ms) {
      return expire_at_ms - now_ms;
    }
    return 0;
  }
};

template <typename T, typename R, typename DURATION>
ValueWithTTL<T> make_ttl_value(R&& v, DURATION ttl) {
  auto expire_at_ms =
      get_timestamp(Timescale::MILLISECOND) + std::chrono::duration_cast<std::chrono::milliseconds>(ttl).count();
  return ValueWithTTL<T>(std::forward<R>(v), expire_at_ms);
}

}  // namespace concurrent_cache