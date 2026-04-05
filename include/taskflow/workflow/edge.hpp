#pragma once

#include <cstddef>
#include <functional>
#include <optional>

namespace taskflow::core {
class task_ctx;
}

namespace taskflow::workflow {

struct edge_def {
  std::size_t from = 0;
  std::size_t to = 0;
  /// Runtime-only: not persisted by serializer::to_json / from_json.
  std::optional<std::function<bool(const core::task_ctx&)>> condition;

  edge_def() = default;
  edge_def(std::size_t from_id, std::size_t to_id) : from(from_id), to(to_id) {}
  edge_def(std::size_t from_id, std::size_t to_id, std::function<bool(const core::task_ctx&)> cond)
      : from(from_id), to(to_id), condition(std::move(cond)) {}
};

}  // namespace taskflow::workflow
