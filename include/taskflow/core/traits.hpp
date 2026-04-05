#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "types.hpp"

namespace taskflow::core {

// Forward declaration
class task_ctx;

namespace detail {

// Compile-time type name extraction
template <typename T>
constexpr std::string_view type_name_impl() {
#if defined(__clang__)
  constexpr std::string_view prefix = "[T = ";
  constexpr std::string_view suffix = "]";
  constexpr std::string_view function = __PRETTY_FUNCTION__;
#elif defined(__GNUC__)
  constexpr std::string_view prefix = "[with T = ";
  constexpr std::string_view suffix = ";";
  constexpr std::string_view function = __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
  constexpr std::string_view prefix = "type_name_impl<";
  constexpr std::string_view suffix = ">(";
  constexpr std::string_view function = __FUNCSIG__;
#else
  return "unknown";
#endif
  const auto start = function.find(prefix);
  if (start == std::string_view::npos) return "unknown";
  const auto end = function.find(suffix, start + prefix.size());
  if (end == std::string_view::npos || start + prefix.size() >= end) return "unknown";
  return function.substr(start + prefix.size(), end - start - prefix.size());
}

// Task callable detection
template <typename T, typename = void>
struct has_callable_operator : std::false_type {};

template <typename T>
struct has_callable_operator<T, std::void_t<decltype(std::declval<T&>()(std::declval<task_ctx&>()))>>
    : std::bool_constant<std::is_same_v<decltype(std::declval<T&>()(std::declval<task_ctx&>())), task_state>> {};

// Static name detection
template <typename T, typename = void>
struct has_static_name : std::false_type {};

template <typename T>
struct has_static_name<T, std::void_t<decltype(T::name)>>
    : std::bool_constant<std::is_convertible_v<decltype(T::name), std::string_view>> {};

// task_ctx value type traits (preserved)
template <typename T>
struct is_basic_task_value
    : std::bool_constant<std::is_same_v<std::decay_t<T>, bool> || std::is_same_v<std::decay_t<T>, int32_t> ||
                         std::is_same_v<std::decay_t<T>, int64_t> || std::is_same_v<std::decay_t<T>, uint32_t> ||
                         std::is_same_v<std::decay_t<T>, uint64_t> || std::is_same_v<std::decay_t<T>, float> ||
                         std::is_same_v<std::decay_t<T>, double> || std::is_same_v<std::decay_t<T>, std::string>> {};

// ADL: unqualified to_task_value(from_task_value) in the trait must find a free function in T's namespace
// (same rules as task_ctx::set/get static_assert messages).
template <typename T, typename = void>
struct has_to_task_value : std::false_type {};

template <typename T>
struct has_to_task_value<T, std::void_t<decltype(to_task_value(std::declval<const T&>()))>> : std::true_type {};

template <typename T, typename = void>
struct has_from_task_value : std::false_type {};

template <typename T>
struct has_from_task_value<T,
                           std::void_t<decltype(from_task_value(std::declval<const std::any&>(), std::declval<T&>()))>>
    : std::true_type {};

}  // namespace detail

// Public Traits

// Compile-time task type name
template <typename T>
constexpr std::string_view task_type_name() {
  if constexpr (detail::has_static_name<T>::value) {
    return T::name;
  } else {
    return detail::type_name_impl<T>();
  }
}

// Core trait: is valid task type
template <typename T>
struct is_task : detail::has_callable_operator<T> {};

template <typename T>
inline constexpr bool is_task_v = is_task<std::decay_t<T>>::value;

// task_ctx value type trait (preserved)
template <typename T>
struct is_task_value
    : std::bool_constant<detail::is_basic_task_value<T>::value ||
                         (detail::has_to_task_value<T>::value && detail::has_from_task_value<T>::value)> {};

template <typename T>
inline constexpr bool is_task_value_v = is_task_value<std::decay_t<T>>::value;

}  // namespace taskflow::core
