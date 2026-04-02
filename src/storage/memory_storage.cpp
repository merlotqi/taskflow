#include <taskflow/storage/memory_storage.hpp>
#include <taskflow/workflow/serializer.hpp>

#include <nlohmann/json.hpp>

namespace tf {

void MemoryStateStorage::save_execution(const WorkflowExecution& ex) {
  std::string blob = execution_to_json(ex).dump();
  std::lock_guard<std::mutex> lock(mutex_);
  blobs_[ex.id] = std::move(blob);
}

bool MemoryStateStorage::load_execution(const ExecutionId& id, WorkflowExecution& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = blobs_.find(id);
  if (it == blobs_.end()) {
    return false;
  }
  nlohmann::json j = nlohmann::json::parse(it->second);
  execution_from_json(j, out);
  return true;
}

void MemoryStateStorage::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  blobs_.clear();
}

}  // namespace tf
