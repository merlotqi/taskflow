#pragma once

#include <optional>
#include <string>
#include <vector>

namespace taskflow::core {

class state_storage {
 public:
  virtual ~state_storage() = default;

  virtual void save(std::size_t exec_id, std::string blob) = 0;

  [[nodiscard]] virtual std::optional<std::string> load(std::size_t exec_id) = 0;

  virtual void remove(std::size_t exec_id) = 0;

  [[nodiscard]] virtual std::vector<std::size_t> list_all() = 0;
};

}  // namespace taskflow::core
