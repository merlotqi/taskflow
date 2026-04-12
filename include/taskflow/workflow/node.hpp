#pragma once

#include <optional>
#include <string>
#include <vector>

#include "taskflow/core/types.hpp"

namespace taskflow::workflow {

struct node_def {
  std::size_t id = 0;
  std::string task_type;
  std::optional<std::string> label;
  std::optional<core::retry_policy> retry;
  /// Optional registered task type run on compensation (Saga-style); omitted or empty => no compensation for this node.
  std::optional<std::string> compensate_task_type;
  /// Retry policy for the compensation task only; if unset, a single attempt is used.
  std::optional<core::retry_policy> compensate_retry;
  std::vector<std::string> tags;

  node_def() = default;
  explicit node_def(std::size_t node_id, std::string type) : id(node_id), task_type(std::move(type)) {}
  node_def(std::size_t node_id, std::string type, std::string lbl)
      : id(node_id), task_type(std::move(type)), label(std::move(lbl)) {}

  [[nodiscard]] std::string display_name() const {
    if (label && !label->empty()) return *label;
    return "node_" + std::to_string(id);
  }
};

struct node_def_hash {
  std::size_t operator()(const node_def& n) const noexcept { return std::hash<std::size_t>{}(n.id); }
};

}  // namespace taskflow::workflow
