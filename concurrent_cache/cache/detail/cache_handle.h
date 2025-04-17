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

#include <functional>
#include <memory>

namespace concurrent_cache {
template <typename T>
class Handle {
 public:
  using DerefHandle = std::shared_ptr<void>;
  explicit Handle(const T* val = nullptr, DerefHandle&& deref = {}) : val_(val), deref_(std::move(deref)) {}
  // explicit Handle(T* val = nullptr, DerefFunc&& deref = {}) : val_(val) {}

  constexpr const T* operator->() const noexcept { return Get(); }
  constexpr const T& operator*() const noexcept { return *Get(); }
  constexpr const T* Get() const noexcept { return val_; }

  constexpr explicit operator bool() const noexcept { return val_; }
  bool operator!=(std::nullptr_t) const noexcept { return val_ != nullptr; }
  bool operator==(std::nullptr_t) const noexcept { return val_ == nullptr; }
  const T* get() const { return val_; }

  void Reset() {
    if (deref_) {
      deref_.reset();
    }
  }

  DerefHandle GetDeref() { return deref_; }

  ~Handle() {}

 private:
  const T* val_;
  DerefHandle deref_;
};

}  // namespace concurrent_cache