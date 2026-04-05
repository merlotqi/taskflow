#pragma once

#include <any>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "taskflow/core/result.hpp"
#include "taskflow/core/result_storage.hpp"

namespace taskflow::storage {

class memory_result_storage : public core::result_storage {
 public:
  memory_result_storage() = default;

  core::result_locator store(std::size_t exec_id, std::size_t node_id, std::string key, std::any value) override;

  std::optional<std::any> load(const core::result_locator& loc) const override;

  bool remove(const core::result_locator& loc) override;

  bool exists(const core::result_locator& loc) const override;

  std::vector<core::result_locator> list(std::size_t exec_id) const override;

  void clear(std::size_t exec_id) override;

  void clear_all() override;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<core::result_locator, std::any, core::result_locator_hash> data_;
};

}  // namespace taskflow::storage
