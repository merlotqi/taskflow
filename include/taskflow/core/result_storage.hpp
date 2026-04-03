#pragma once

#include <any>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "taskflow/core/result.hpp"

namespace taskflow::core {

class result_storage {
 public:
  virtual ~result_storage() = default;

  virtual result_locator store(std::size_t exec_id, std::size_t node_id, std::string key, std::any value) = 0;

  virtual std::optional<std::any> load(const result_locator& loc) const = 0;

  virtual bool remove(const result_locator& loc) = 0;

  virtual bool exists(const result_locator& loc) const = 0;

  virtual std::vector<result_locator> list(std::size_t exec_id) const = 0;

  virtual void clear(std::size_t exec_id) = 0;

  virtual void clear_all() = 0;
};

}  // namespace taskflow::core
