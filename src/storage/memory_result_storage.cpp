#include "taskflow/storage/memory_result_storage.hpp"

namespace taskflow::storage {

core::result_locator memory_result_storage::store(std::size_t exec_id,
                                                    std::size_t node_id,
                                                    std::string key,
                                                    std::any value) {
  std::lock_guard lock(mutex_);
  core::result_locator loc{node_id, std::move(key)};
  data_[loc] = std::move(value);
  return loc;
}

std::optional<std::any> memory_result_storage::load(const core::result_locator& loc) const {
  std::lock_guard lock(mutex_);
  auto it = data_.find(loc);
  if (it == data_.end()) return std::nullopt;
  return it->second;
}

bool memory_result_storage::remove(const core::result_locator& loc) {
  std::lock_guard lock(mutex_);
  return data_.erase(loc) > 0;
}

bool memory_result_storage::exists(const core::result_locator& loc) const {
  std::lock_guard lock(mutex_);
  return data_.find(loc) != data_.end();
}

std::vector<core::result_locator> memory_result_storage::list(std::size_t exec_id) const {
  std::lock_guard lock(mutex_);
  std::vector<core::result_locator> result;
  for (const auto& [loc, _] : data_) {
    result.push_back(loc);
  }
  return result;
}

void memory_result_storage::clear(std::size_t exec_id) {
  std::lock_guard lock(mutex_);
  data_.clear();
}

void memory_result_storage::clear_all() {
  std::lock_guard lock(mutex_);
  data_.clear();
}

}  // namespace taskflow::storage
