#pragma once

#include <cstddef>
#include <vector>

namespace taskflow::engine {
class workflow_execution;
}

namespace taskflow::engine {

class scheduler {
 public:
  [[nodiscard]] static std::vector<std::size_t> ready_nodes(const workflow_execution& execution);
  [[nodiscard]] static std::size_t pick_next(const workflow_execution& execution);
  [[nodiscard]] static std::vector<std::size_t> ready_nodes_ordered(const workflow_execution& execution);
  [[nodiscard]] static bool has_pending(const workflow_execution& execution);
};

}  // namespace taskflow::engine
