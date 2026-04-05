#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>

#include "traits.hpp"
#include "types.hpp"

#if __cplusplus >= 202002L
#include "task_concepts.hpp"
#endif

namespace taskflow::core {

class task_ctx;

class task_wrapper {
  struct concept_t {
    virtual ~concept_t() = default;
    [[nodiscard]] virtual task_state execute(task_ctx& ctx) = 0;
    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
  };

  template <typename T>
  struct model_t : concept_t {
    T task_;
    explicit model_t(T t) : task_(std::move(t)) {}

    task_state execute(task_ctx& ctx) override { return task_(ctx); }

    std::string_view type_name() const noexcept override { return task_type_name<T>(); }
  };

  std::unique_ptr<concept_t> impl_;

 public:
  task_wrapper() = default;

#if __cplusplus >= 202002L
  template <typename T>
    requires task_callable<std::decay_t<T>>
  explicit task_wrapper(T task) : impl_(std::make_unique<model_t<T>>(std::move(task))) {}
#else
  template <typename T, std::enable_if_t<is_task_v<T>, int> = 0>
  explicit task_wrapper(T task) : impl_(std::make_unique<model_t<T>>(std::move(task))) {}
#endif

  task_wrapper(task_wrapper&&) noexcept = default;
  task_wrapper& operator=(task_wrapper&&) noexcept = default;

  task_wrapper(const task_wrapper&) = delete;
  task_wrapper& operator=(const task_wrapper&) = delete;

  [[nodiscard]] explicit operator bool() const noexcept { return impl_ != nullptr; }
  [[nodiscard]] task_state execute(task_ctx& ctx) { return impl_->execute(ctx); }
  [[nodiscard]] std::string_view type_name() const noexcept { return impl_->type_name(); }
};

/// Callable returning a new task instance (supports captures; prefer over raw function pointers).
using task_factory = std::function<task_wrapper()>;

template <typename T, std::enable_if_t<is_task_v<T>, int> = 0>
task_wrapper make_task() {
  return task_wrapper{T{}};
}

template <typename T, std::enable_if_t<is_task_v<T>, int> = 0>
task_wrapper make_task(T task) {
  return task_wrapper{std::move(task)};
}

}  // namespace taskflow::core
