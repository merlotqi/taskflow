#pragma once

// C++20 mirror of is_task_v for clearer diagnostics when building with -std=c++20 (see TASKFLOW_CXX20).

#if __cplusplus >= 202002L

#include <concepts>

#include "types.hpp"

namespace taskflow::core {

class task_ctx;

template <typename T>
concept task_callable = requires(T& t, task_ctx& ctx) {
  { t(ctx) } -> std::same_as<task_state>;
};

}  // namespace taskflow::core

#endif
