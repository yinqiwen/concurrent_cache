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

// static const auto g_app_start_time = std::chrono::system_clock::now();

enum class Timescale {
  SECOND,
  MILLISECOND,
  MICROSECOND,
};

inline uint32_t get_timestamp(Timescale scale) {
  auto now = gettimeofday_us();
  auto duration = now - g_app_start_time;
  switch (scale) {
    case Timescale::MILLISECOND: {
      auto millis = duration / 1000;
      return static_cast<uint32_t>(millis);
    }
    case Timescale::MICROSECOND: {
      auto micros = duration / 1000000;
      return static_cast<uint32_t>(micros);
    }
    case Timescale::SECOND:
    default: {
      auto secs = duration / 1000000000;
      return static_cast<uint32_t>(secs);
    }
  }
}

}  // namespace concurrent_cache