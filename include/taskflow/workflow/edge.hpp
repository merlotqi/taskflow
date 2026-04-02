#pragma once

#include <taskflow/core/types.hpp>
#include <string>

namespace tf {

struct EdgeDef {
  NodeId from;
  NodeId to;
  std::string condition;
};

}  // namespace tf
