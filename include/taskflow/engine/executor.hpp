#pragma once

#include <cstddef>
#include <vector>

#include "taskflow/core/types.hpp"

namespace taskflow::engine {
class workflow_execution;
class itask_registry;
}  // namespace taskflow::engine

namespace taskflow::observer {
class observer;
}

namespace taskflow::engine {

class executor {
 public:
  [[nodiscard]] static core::task_state execute_node(workflow_execution& execution, std::size_t node_id,
                                                     const itask_registry& registry,
                                                     const std::vector<observer::observer*>& observers = {});

  [[nodiscard]] static core::task_state execute_with_retry(workflow_execution& execution, std::size_t node_id,
                                                           const itask_registry& registry,
                                                           const std::vector<observer::observer*>& observers = {});
};

}  // namespace taskflow::engine
