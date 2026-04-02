#pragma once

#include <taskflow/core/types.hpp>
#include <string>

namespace tf {

struct NodeDef {
  NodeId id;
  std::string task_type;
  int max_retries{0};
};

}  // namespace tf
