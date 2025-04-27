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

#include <folly/Singleton.h>
#include <sys/time.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "cache.h"
#include "concurrent_cache/benchmark/cache.h"
#include "concurrent_cache/benchmark/key_generator.h"
#include "fmt/printf.h"
#include "folly/ThreadCachedInt.h"
#include "folly/synchronization/HazptrThreadPoolExecutor.h"
#include "gflags/gflags.h"

DEFINE_uint64(cache_size, 1000000, "max cache size");
DEFINE_uint64(max_key_multiplier, 5, "max key range multiplier for cache size");
DEFINE_uint32(hit_ratio, 80, "cache hit ratio");
DEFINE_uint32(threads, 16, "test threads");

DEFINE_uint64(N, 100000000, "test count");
// DEFINE_uint32(key_size, 16, "cache key size");
DEFINE_uint32(val_size, 16, "cache value size");
DEFINE_double(zipfian_theta, 0.99, "zipfian theta");
DEFINE_string(test, "get", "test method, support get/set/getset");
DEFINE_string(cache, "concurrent_lru",
              "cache type, support concurrent_lru/folly_lru/concurrent_fixed_hashmap/folly_concurrent_hashmap");
DEFINE_string(generator, "zipfian", "generator type, support uniform/zipfian");

namespace concurrent_cache {
namespace benchmark {
using BenchFunc = std::function<void()>;
using BenchLogFunc = std::function<void()>;

static std::unique_ptr<TestCache> g_test_cache;
static std::unique_ptr<KeyGeneator> g_key_generator;

static folly::ThreadCachedInt<int64_t> g_write_fail{0};
static folly::ThreadCachedInt<int64_t> g_write_success{0};

static folly::ThreadCachedInt<int64_t> g_test_count{0};
static folly::ThreadCachedInt<int64_t> g_read_hit{0};
static folly::ThreadCachedInt<int64_t> g_read_miss{0};

static size_t g_cache_max_size = 0;
static int64_t g_last_log_ts = 0;

const double kAlpha = 1.0;  // Shapes zipf distribution
const int kZipfSeed = 111;  // Seed for Zipf distribution

size_t get_mem_usage() {
  std::ifstream status_file("/proc/self/status");
  std::string line;
  size_t memory_kb = 0;

  while (std::getline(status_file, line)) {
    if (line.find("VmRSS") != std::string::npos) {  // 物理内存占用
      sscanf(line.c_str(), "VmRSS: %zu kB", &memory_kb);
      break;
    }
  }
  return memory_kb;
}

struct BenchmarkStage {
  std::string name;
  BenchFunc run;
  BenchLogFunc log;
  uint32_t threads = 1;
  uint64_t test_count = 1;
};

struct Benchmark {
  void AddStage(const std::string& name, BenchFunc&& f, BenchLogFunc&& log, uint32_t threads, uint64_t test_count) {
    BenchmarkStage stage;
    stage.name = name;
    stage.run = std::move(f);
    stage.log = std::move(log);
    stage.threads = threads;
    stage.test_count = test_count;
    stages.emplace_back(std::move(stage));
  }
  void Run() {
    for (auto& stage : stages) {
      fmt::print("=================Start Stage:{}, Threads:{}==================\n", stage.name, stage.threads);
      std::vector<std::thread> tasks;
      uint64_t count_per_thread = stage.test_count / stage.threads;
      uint64_t rest = stage.test_count % stage.threads;
      std::atomic<uint32_t> finished{0};

      for (uint32_t i = 0; i < stage.threads; i++) {
        uint64_t n = count_per_thread;
        if (i == stage.threads - 1) {
          n += rest;
        }
        tasks.emplace_back(std::thread([&stage, &finished, n]() {
          for (uint64_t k = 0; k < n; k++) {
            stage.run();
          }
          finished++;
        }));
      }
      tasks.emplace_back(std::thread([&]() {
        using namespace std::chrono_literals;
        g_last_log_ts = gettimeofday_us();
        while (finished.load() < stage.threads) {
          if (gettimeofday_us() - g_last_log_ts >= 3000 * 1000) {
            stage.log();
            g_last_log_ts = gettimeofday_us();
          }
          std::this_thread::sleep_for(1ms);
        }
      }));
      for (auto& task : tasks) {
        task.join();
      }
      stage.log();
      fmt::print("=================End Stage:{}==================\n\n", stage.name);
    }
  }

  std::vector<BenchmarkStage> stages;
};

static inline auto& rgen() {
  static std::random_device rd;
  static std::atomic<uint32_t> seed{111};
  thread_local std::mt19937_64 generator(rd());
  thread_local bool set_seed = false;
  if (!set_seed) {
    generator.seed(seed.fetch_add(111));
    set_seed = true;
  }
  return generator;
}

static inline uint64_t random_id() {
  // std::uniform_int_distribution<uint64_t> dist0(0, 10000);
  // if (dist0(rgen()) < FLAGS_hit_ratio * 100) {
  //   std::uniform_int_distribution<std::uint64_t> dist1(0, g_cache_max_size);
  //   return dist1(rgen());
  // } else {
  //   std::uniform_int_distribution<std::uint64_t> dist1(g_cache_max_size, g_cache_max_size *
  //   FLAGS_max_key_multiplier); return dist1(rgen());
  // }
  // return (*g_generator)(rgen());
  return g_key_generator->next();
}

static inline std::pair<uint64_t, std::string> rand_key_value() {
  auto rand = random_id();
  std::string val;
  uint64_t ik = rand;
  uint64_t iv = rand + 1;
  // key.append(reinterpret_cast<const char*>(&ik), sizeof(uint64_t));
  val.append(reinterpret_cast<const char*>(&iv), sizeof(uint64_t));
  // key.resize(FLAGS_key_size);
  val.resize(FLAGS_val_size);
  return {ik, val};
}

static void init_cache() {
  CacheOptions opts;
  opts.max_size = FLAGS_cache_size;
  // g_test_cache = std::make_unique<LRU>();
  if (FLAGS_cache == "concurrent_lru") {
    g_test_cache = std::make_unique<LRU>();
  } else if (FLAGS_cache == "folly_lru") {
    g_test_cache = std::make_unique<FollyLRU>();
  } else if (FLAGS_cache == "concurrent_hashmap") {
    g_test_cache = std::make_unique<HashMap>();
  } else if (FLAGS_cache == "folly_concurrent_hashmap") {
    g_test_cache = std::make_unique<FollyHashMap>();
  } else if (FLAGS_cache == "folly_atomic_hashmap") {
    g_test_cache = std::make_unique<FollyAtomicHashMap>();
  }

  if (FLAGS_generator == "uniform") {
    g_key_generator = std::make_unique<UniformGenerator>(FLAGS_cache_size, FLAGS_hit_ratio);
  } else if (FLAGS_generator == "zipfian") {
    g_key_generator =
        std::make_unique<ZipfianGenerator>(FLAGS_cache_size * FLAGS_max_key_multiplier, FLAGS_zipfian_theta);
  }

  g_test_cache->Init(FLAGS_cache_size);
  // g_cache_max_size = static_cast<size_t>(g_test_cache->Capacity() * FLAGS_hit_ratio * 1.0 / 100);
  g_cache_max_size = FLAGS_cache_size;

  fmt::print("g_cache_max_size:{}\n", g_cache_max_size);
}

static void fill_cache(bool rand) {
  for (uint64_t i = 0; i < FLAGS_cache_size; i++) {
    std::string key, val;
    uint64_t ik = i;
    uint64_t iv = i + 1;
    if (rand) {
      auto rand = random_id();
      ik = rand;
      iv = rand + 1;
    }
    // key.append(reinterpret_cast<const char*>(&ik), sizeof(uint64_t));
    val.append(reinterpret_cast<const char*>(&iv), sizeof(uint64_t));
    // key.resize(FLAGS_key_size);
    val.resize(FLAGS_val_size);
    auto rc = g_test_cache->Put(ik, std::move(val));
    if (!rc) {
      ++g_write_fail;
      // fmt::print("fill fail:{}\n", static_cast<int>(rc));
    } else {
      ++g_write_success;
    }
  }
}
static void fill_cache_log() {
  auto write_success = g_write_success.readFull();
  auto write_fail = g_write_fail.readFull();
  auto now_write = write_success + write_fail;
  static int64_t last_write_count = 0;

  fmt::print("fill_success:{},fill_fail:{},fill_qps:{},mem_usage:{}KB \n", write_success, write_fail,
             (now_write - last_write_count) * 1000000.0 / (gettimeofday_us() - g_last_log_ts), get_mem_usage());

  last_write_count = now_write;
}

static void read_cache() {
  auto [test_key, test_val] = rand_key_value();
  auto handle = g_test_cache->Get(test_key, {});
  if (handle) {
    ++g_read_hit;
  } else {
    ++g_read_miss;
  }
}

static void read_cache_log() {
  auto read_hit = g_read_hit.readFull();
  auto read_miss = g_read_miss.readFull();

  auto now_read = read_hit + read_miss;
  static int64_t last_read_count = 0;

  fmt::print("get_hit:{},get_miss:{},hit_ratio:{},get_ops:{},mem_usage:{}KB\n", read_hit, read_miss,
             read_hit * 1.0 / (read_hit + read_miss),
             (now_read - last_read_count) * 1000000.0 / (gettimeofday_us() - g_last_log_ts), get_mem_usage());
  last_read_count = now_read;
}

static void write_cache() {
  auto [test_key, test_val] = rand_key_value();
  auto rc = g_test_cache->Put(test_key, std::move(test_val));

  if (rc) {
    ++g_write_success;
  } else {
    ++g_write_fail;
  }
}

static void write_cache_log() {
  auto write_success = g_write_success.readFull();
  auto write_fail = g_write_fail.readFull();
  auto now_write = write_success + write_fail;
  static int64_t last_write_count = 0;

  fmt::print("set_success:{},set_fail:{},set_ops:{},mem_usage:{}KB \n", write_success, write_fail,
             (now_write - last_write_count) * 1000000.0 / (gettimeofday_us() - g_last_log_ts), get_mem_usage());
  fmt::print("Cache Status:{}\n", g_test_cache->GetStats());
  last_write_count = now_write;
}

static void read_write_cache() {
  auto [test_key, test_val] = rand_key_value();
  auto handle = g_test_cache->Get(test_key, {});
  if (handle) {
    ++g_read_hit;
  } else {
    ++g_read_miss;
    auto rc = g_test_cache->Put(test_key, std::move(test_val));
    if (rc) {
      ++g_write_success;
    } else {
      ++g_write_fail;
    }
  }
}

static void read_write_cache_log() {
  auto read_hit = g_read_hit.readFull();
  auto read_miss = g_read_miss.readFull();
  auto now_read = read_hit + read_miss;
  auto write_success = g_write_success.readFull();
  auto write_fail = g_write_fail.readFull();
  static int64_t last_read_count = 0;
  static int64_t last_read_miss = 0;
  static int64_t last_read_hit = 0;
  static int64_t last_write = 0;

  auto hit_ratio = (read_hit - last_read_hit) * 1.0 / (now_read - last_read_count);
  auto getset_ops = (now_read - last_read_count) * 1000000.0 / (gettimeofday_us() - g_last_log_ts);

  fmt::print("get_hit:{},get_miss:{},hit_ratio:{},set_succss:{},set_fail:{},getset_ops:{},mem_usage:{}KB \n", read_hit,
             read_miss, hit_ratio, write_success, write_fail, getset_ops, get_mem_usage());
  fmt::print("Cache Status:{}\n", g_test_cache->GetStats());
  last_read_count = now_read;
  last_read_miss = read_miss;
  last_read_hit = read_hit;
  last_write = (last_write + write_fail);
}

static void start_test() {
  init_cache();

  Benchmark bench;
  if (FLAGS_test == "get") {
    bench.AddStage("fill_cache", std::bind(fill_cache, false), fill_cache_log, 1, 1);
    bench.AddStage("read_cache", read_cache, read_cache_log, FLAGS_threads, FLAGS_N);
  } else if (FLAGS_test == "set") {
    bench.AddStage("write_cache", write_cache, write_cache_log, FLAGS_threads, FLAGS_N);
  } else if (FLAGS_test == "getset") {
    bench.AddStage("fill_cache", std::bind(fill_cache, false), fill_cache_log, 1, 1);
    bench.AddStage("read_write_cache", read_write_cache, read_write_cache_log, FLAGS_threads, FLAGS_N);
  }
  bench.Run();
}
}  // namespace benchmark
}  // namespace concurrent_cache

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, false);
  if (FLAGS_test != "get" && FLAGS_test != "set" && FLAGS_test != "getset") {
    fmt::print("Invalid '-test' argument!\n");
    return -1;
  }
  folly::SingletonVault::singleton()->registrationComplete();
  // folly::enable_hazptr_thread_pool_executor();
  concurrent_cache::benchmark::start_test();
  return 0;
}