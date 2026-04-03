#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace taskflow::core {

struct result_locator {
  std::size_t node_id;
  std::string key;

  bool operator==(const result_locator& other) const { return node_id == other.node_id && key == other.key; }

  bool operator!=(const result_locator& other) const { return !(*this == other); }
};

struct result_locator_hash {
  std::size_t operator()(const result_locator& loc) const {
    return std::hash<std::size_t>{}(loc.node_id) ^ (std::hash<std::string>{}(loc.key) << 1);
  }
};

}  // namespace taskflow::core
