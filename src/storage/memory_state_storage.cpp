#include "taskflow/storage/memory_state_storage.hpp"

namespace taskflow::storage {

void memory_state_storage::save(std::size_t exec_id, std::string blob) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_[exec_id] = std::move(blob);
}

std::optional<std::string> memory_state_storage::load(std::size_t exec_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = data_.find(exec_id);
  if (it == data_.end()) return std::nullopt;
  return it->second;
}

void memory_state_storage::remove(std::size_t exec_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.erase(exec_id);
}

std::vector<std::size_t> memory_state_storage::list_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::size_t> ids;
  ids.reserve(data_.size());
  for (const auto& [id, _] : data_) ids.push_back(id);
  return ids;
}

std::size_t memory_state_storage::size() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_.size();
}

void memory_state_storage::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.clear();
}

}  // namespace taskflow::storage
