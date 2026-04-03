#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "taskflow/core/cancellation_token.hpp"
#include "taskflow/engine/result_collector.hpp"
#include "traits.hpp"

namespace taskflow::core {

class task_ctx {
 public:
  task_ctx();
  task_ctx(const task_ctx&) = delete;
  task_ctx& operator=(const task_ctx&) = delete;
  task_ctx(task_ctx&& other) noexcept;
  task_ctx& operator=(task_ctx&& other) noexcept;

  template <typename T, std::enable_if_t<detail::is_basic_task_value<T>::value, int> = 0>
  void set(std::string_view key, T value) {
    std::lock_guard<std::mutex> lock(*data_mutex_);
    data_[std::string(key)] = std::move(value);
  }

  template <typename T,
            std::enable_if_t<!detail::is_basic_task_value<T>::value && detail::has_to_task_value<T>::value, int> = 0>
  void set(std::string_view key, const T& value) {
    std::lock_guard<std::mutex> lock(*data_mutex_);
    data_[std::string(key)] = to_task_value(value);
  }

  template <typename T,
            std::enable_if_t<!detail::is_basic_task_value<T>::value && !detail::has_to_task_value<T>::value, int> = 0>
  void set(std::string_view, const T&) {
    static_assert(sizeof(T) == 0,
                  "task_ctx::set: Type not supported. "
                  "Define to_task_value(const T&) -> std::any "
                  "in the same namespace as T (ADL).");
  }

  template <typename T, std::enable_if_t<detail::is_basic_task_value<T>::value, int> = 0>
  [[nodiscard]] std::optional<T> get(std::string_view key) const {
    std::lock_guard<std::mutex> lock(*data_mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    try {
      return std::any_cast<T>(it->second);
    } catch (const std::bad_any_cast&) {
      return std::nullopt;
    }
  }

  template <typename T,
            std::enable_if_t<!detail::is_basic_task_value<T>::value && detail::has_from_task_value<T>::value, int> = 0>
  [[nodiscard]] std::optional<T> get(std::string_view key) const {
    std::lock_guard<std::mutex> lock(*data_mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    T result{};
    if (from_task_value(it->second, result)) {
      return result;
    }
    return std::nullopt;
  }

  template <typename T,
            std::enable_if_t<!detail::is_basic_task_value<T>::value && !detail::has_from_task_value<T>::value, int> = 0>
  [[nodiscard]] std::optional<T> get(std::string_view) const {
    static_assert(sizeof(T) == 0,
                  "task_ctx::get: Type not supported. "
                  "Define from_task_value(const std::any&, T&) -> bool "
                  "in the same namespace as T (ADL).");
    return std::nullopt;
  }

  [[nodiscard]] bool contains(std::string_view key) const;
  /// Raw map access without locking. Not thread-safe vs concurrent set/get; use only single-threaded or after run.
  [[nodiscard]] const std::unordered_map<std::string, std::any>& data() const noexcept;
  [[nodiscard]] std::unordered_map<std::string, std::any>& data() noexcept;
  void set_data(std::unordered_map<std::string, std::any> d);

  void report_progress(float progress);
  [[nodiscard]] float progress() const noexcept;

  void cancel();
  [[nodiscard]] bool is_cancelled() const noexcept;
  void set_cancellation_token(cancellation_token token);
  [[nodiscard]] const cancellation_token& get_cancellation_token() const noexcept;

  [[nodiscard]] std::size_t node_id() const noexcept;
  void set_node_id(std::size_t id) noexcept;

  [[nodiscard]] std::size_t exec_id() const noexcept;
  void set_exec_id(std::size_t id) noexcept;

  [[nodiscard]] std::int64_t exec_start_time() const noexcept;
  void set_exec_start_time(std::int64_t t) noexcept;

  // Result collection
  void set_collector(engine::result_collector* collector) noexcept;

  template <typename T, std::enable_if_t<detail::is_basic_task_value<T>::value, int> = 0>
  void set_result(std::string key, T value) {
    if (collector_) {
      collector_->set(node_id_, std::move(key), std::move(value));
    }
  }

  template <typename T,
            std::enable_if_t<!detail::is_basic_task_value<T>::value && detail::has_to_task_value<T>::value, int> = 0>
  void set_result(std::string key, const T& value) {
    if (collector_) {
      collector_->set(node_id_, std::move(key), value);
    }
  }

  template <typename T, std::enable_if_t<detail::is_basic_task_value<T>::value, int> = 0>
  [[nodiscard]] std::optional<T> get_result(std::size_t from_node, const std::string& key) const {
    return collector_ ? collector_->get<T>(from_node, key) : std::nullopt;
  }

  template <typename T,
            std::enable_if_t<!detail::is_basic_task_value<T>::value && detail::has_from_task_value<T>::value, int> = 0>
  [[nodiscard]] std::optional<T> get_result(std::size_t from_node, const std::string& key) const {
    return collector_ ? collector_->get<T>(from_node, key) : std::nullopt;
  }

 private:
  mutable std::unique_ptr<std::mutex> data_mutex_;
  std::unordered_map<std::string, std::any> data_;
  std::atomic<float> progress_{0.0f};
  std::atomic<bool> cancelled_{false};
  cancellation_token cancellation_token_;
  std::size_t node_id_ = 0;
  std::size_t exec_id_ = 0;
  std::int64_t exec_start_time_ = 0;
  engine::result_collector* collector_ = nullptr;
};

}  // namespace taskflow::core
