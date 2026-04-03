#pragma once

#include <any>

namespace taskflow::cpo {

/// Explicit customization point: calls unqualified `to_task_value` so ADL finds overloads in `T`'s namespace.
template <typename T>
auto adl_to_task_value(const T& value) noexcept(noexcept(to_task_value(value))) -> decltype(to_task_value(value)) {
  return to_task_value(value);
}

/// Explicit customization point: calls unqualified `from_task_value` for ADL.
template <typename T>
auto adl_from_task_value(const std::any& storage, T& out) noexcept(noexcept(from_task_value(storage, out)))
    -> decltype(from_task_value(storage, out)) {
  return from_task_value(storage, out);
}

}  // namespace taskflow::cpo
