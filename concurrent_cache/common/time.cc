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
#include "concurrent_cache/common/time.h"
#include <folly/executors/FunctionScheduler.h>
#include <chrono>
#include <memory>
namespace concurrent_cache {
static std::unique_ptr<folly::FunctionScheduler> g_tick_updater;
static int64_t g_app_start_us = gettimeofday_us();
static int64_t g_app_start_ms = gettimeofday_us() / 1000;
static int64_t g_app_start_s = gettimeofday_us() / 1000000;
static int64_t g_current_us = gettimeofday_us();
static int64_t g_current_ms = gettimeofday_us() / 1000;
static int64_t g_current_s = gettimeofday_us() / 1000000;

static void update_timestatmp() {
  g_current_us = gettimeofday_us();
  g_current_ms = g_current_us / 1000;
  g_app_start_s = g_current_us / 1000000;
}

void enable_estimate_timestamp_updater() {
  if (!g_tick_updater) {
    g_tick_updater = std::make_unique<folly::FunctionScheduler>();
    g_tick_updater->addFunction(update_timestatmp, std::chrono::milliseconds(1), "");
    g_tick_updater->start();
  }
}

uint32_t get_timestamp(Timescale scale) {
  switch (scale) {
    case Timescale::MILLISECOND: {
      return static_cast<uint32_t>(g_current_ms - g_app_start_ms);
    }
    case Timescale::MICROSECOND: {
      return static_cast<uint32_t>(g_current_us - g_app_start_us);
    }
    case Timescale::SECOND:
    default: {
      return static_cast<uint32_t>(g_current_s - g_app_start_s);
    }
  }
}

}  // namespace concurrent_cache