#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace taskflow::core {

using task_id = std::uint64_t;
using exec_id = std::uint64_t;
using node_id = std::uint64_t;

enum class task_state : std::uint8_t {
  pending = 0,
  running = 1,
  success = 2,
  failed = 3,
  retry = 4,
  skipped = 5,
  cancelled = 6,
};

[[nodiscard]] constexpr std::string_view to_string(task_state state) noexcept {
  switch (state) {
    case task_state::pending:
      return "pending";
    case task_state::running:
      return "running";
    case task_state::success:
      return "success";
    case task_state::failed:
      return "failed";
    case task_state::retry:
      return "retry";
    case task_state::skipped:
      return "skipped";
    case task_state::cancelled:
      return "cancelled";
  }
  return "unknown";
}

[[nodiscard]] constexpr task_state parse_state(std::string_view s) noexcept {
  if (s == "pending") return task_state::pending;
  if (s == "running") return task_state::running;
  if (s == "success") return task_state::success;
  if (s == "failed") return task_state::failed;
  if (s == "retry") return task_state::retry;
  if (s == "skipped") return task_state::skipped;
  if (s == "cancelled") return task_state::cancelled;
  return task_state::pending;
}

enum class task_priority : std::int32_t {
  low = 0,
  normal = 50,
  high = 100,
};

struct retry_policy {
  std::int32_t max_attempts = 1;
  std::int32_t initial_delay_ms = 0;
  float backoff_multiplier = 1.0f;
};

struct node_state {
  node_id id;
  task_state state = task_state::pending;
  std::int32_t retry_count = 0;
  std::int64_t started_at = 0;
  std::int64_t finished_at = 0;
  std::string error_message;
};

struct idempotency_key {
  std::size_t exec_id = 0;
  std::size_t node_id = 0;

  bool operator==(const idempotency_key& other) const { return exec_id == other.exec_id && node_id == other.node_id; }

  bool operator!=(const idempotency_key& other) const { return !(*this == other); }
};

struct idempotency_key_hash {
  std::size_t operator()(const idempotency_key& key) const {
    return std::hash<std::size_t>{}(key.exec_id) ^ (std::hash<std::size_t>{}(key.node_id) << 1);
  }
};

}  // namespace taskflow::core
