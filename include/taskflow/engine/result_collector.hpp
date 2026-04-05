#pragma once

#include <any>
#include <optional>
#include <string>
#include <vector>

#include "taskflow/core/result.hpp"
#include "taskflow/core/result_storage.hpp"
#include "taskflow/core/traits.hpp"

namespace taskflow::engine {

class result_collector {
 public:
  explicit result_collector(core::result_storage* storage = nullptr) : storage_(storage) {}

  // Store result - basic types
  template <typename T, std::enable_if_t<core::detail::is_basic_task_value<T>::value, int> = 0>
  core::result_locator set(std::size_t exec_id, std::size_t node_id, std::string key, T value) {
    if (storage_) {
      return storage_->store(exec_id, node_id, std::move(key), std::move(value));
    }
    return core::result_locator{node_id, std::move(key)};
  }

  // Store result - custom types with to_task_value
  template <typename T,
            std::enable_if_t<!core::detail::is_basic_task_value<T>::value && core::detail::has_to_task_value<T>::value,
                             int> = 0>
  core::result_locator set(std::size_t exec_id, std::size_t node_id, std::string key, const T& value) {
    if (storage_) {
      return storage_->store(exec_id, node_id, std::move(key), to_task_value(value));
    }
    return core::result_locator{node_id, std::move(key)};
  }

  // Get result - basic types
  template <typename T, std::enable_if_t<core::detail::is_basic_task_value<T>::value, int> = 0>
  [[nodiscard]] std::optional<T> get(const core::result_locator& loc) const {
    if (!storage_) return std::nullopt;
    auto val = storage_->load(loc);
    if (!val) return std::nullopt;
    try {
      return std::any_cast<T>(*val);
    } catch (const std::bad_any_cast&) {
      return std::nullopt;
    }
  }

  // Get result - custom types with from_task_value
  template <typename T,
            std::enable_if_t<
                !core::detail::is_basic_task_value<T>::value && core::detail::has_from_task_value<T>::value, int> = 0>
  [[nodiscard]] std::optional<T> get(const core::result_locator& loc) const {
    if (!storage_) return std::nullopt;
    auto val = storage_->load(loc);
    if (!val) return std::nullopt;
    T result{};
    if (from_task_value(*val, result)) {
      return result;
    }
    return std::nullopt;
  }

  // Check if result exists
  [[nodiscard]] bool has(const core::result_locator& loc) const { return storage_ && storage_->exists(loc); }

  // List all result locators
  [[nodiscard]] std::vector<core::result_locator> list(std::size_t exec_id) const {
    if (!storage_) return {};
    return storage_->list(exec_id);
  }

  // Clear results for an execution
  void clear(std::size_t exec_id) {
    if (storage_) storage_->clear(exec_id);
  }

  // Clear all results
  void clear_all() {
    if (storage_) storage_->clear_all();
  }

  // Set storage backend
  void set_storage(core::result_storage* storage) { storage_ = storage; }

  // Get storage backend
  [[nodiscard]] core::result_storage* storage() const noexcept { return storage_; }

 private:
  core::result_storage* storage_ = nullptr;
};

}  // namespace taskflow::engine
