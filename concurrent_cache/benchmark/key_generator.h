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

#include <cassert>
#include <cmath>
#include <cstddef>
#include <random>

namespace concurrent_cache {
namespace benchmark {

struct KeyGeneator {
  virtual uint64_t next() = 0;
  virtual ~KeyGeneator() {}
};

class UniformGenerator : public KeyGeneator {
 public:
  UniformGenerator(size_t max, uint32_t hit_ratio) : max_(max), hit_ratio_(hit_ratio) {}

  uint64_t next() override {
    static std::random_device rd;
    static thread_local std::mt19937_64 generator(rd());
    std::uniform_int_distribution<size_t> dist(0, max_ / hit_ratio_ * 100);
    return dist(generator);
  }

 private:
  size_t max_;
  uint32_t hit_ratio_;
};

class ZipfianGenerator : public KeyGeneator {
 public:
  ZipfianGenerator(size_t n, double theta) : n_(n), alpha_(1 / (1 - theta)), zeta2_(1 + std::pow(2.0, -theta)) {
    assert(n > 2);
    zetan_ = zeta2_;
    for (size_t i = 3; i <= n; ++i) {
      zetan_ += std::pow((double)i, -theta);
    }
    eta_ = (1 - std::pow(2.0 / n, 1 - theta)) / (1 - zeta2_ / zetan_);
  }
  uint64_t next() override {
    static std::random_device rd;
    static thread_local std::mt19937_64 generator(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    double u = dis(generator);
    double uz = u * zetan_;
    if (uz < 1) return 1;
    if (uz < zeta2_) return 2;
    return 1 + (size_t)(n_ * std::pow(eta_ * u - eta_ + 1, alpha_));
  }

 private:
  size_t n_;
  double alpha_;
  double zeta2_;
  double zetan_;
  double eta_;
};

}  // namespace benchmark
}  // namespace concurrent_cache