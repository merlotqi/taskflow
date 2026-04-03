#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "taskflow/core/state_storage.hpp"

namespace taskflow::storage {

class memory_state_storage : public core::state_storage {
 public:
  void save(std::size_t exec_id, std::string blob) override;
  [[nodiscard]] std::optional<std::string> load(std::size_t exec_id) override;
  void remove(std::size_t exec_id) override;
  [[nodiscard]] std::vector<std::size_t> list_all() override;

  [[nodiscard]] std::size_t size() const noexcept;
  void clear();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::size_t, std::string> data_;
};

}  // namespace taskflow::storage
