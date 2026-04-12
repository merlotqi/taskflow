#pragma once

#include <cstddef>
#include <vector>

#include "taskflow/core/types.hpp"

namespace taskflow::engine {
class workflow_execution;
class itask_registry;
}  // namespace taskflow::engine

namespace taskflow::obs {
class observer;
}

namespace taskflow::engine {

class executor {
 public:
  [[nodiscard]] static core::task_state execute_node(workflow_execution& execution, std::size_t node_id,
                                                     const itask_registry& registry,
                                                     const std::vector<obs::observer*>& observers = {});

  [[nodiscard]] static core::task_state execute_with_retry(workflow_execution& execution, std::size_t node_id,
                                                           const itask_registry& registry,
                                                           const std::vector<obs::observer*>& observers = {});

  /// Runs the node's optional compensate task after forward `success`; transitions success→compensating→compensated
  /// or compensation_failed. No-op (returns success) if no compensate_task_type or node is not `success`.
  /// If `respect_cancel` is false, cancellation is ignored so `compensate_on_cancel` can run.
  [[nodiscard]] static core::task_state execute_compensation(workflow_execution& execution, std::size_t node_id,
                                                             const itask_registry& registry,
                                                             const std::vector<obs::observer*>& observers = {},
                                                             bool respect_cancel = true);
};

}  // namespace taskflow::engine
