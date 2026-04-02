#pragma once

#include <taskflow/storage/storage.hpp>

#include <mutex>
#include <unordered_map>

namespace tf {

class MemoryStateStorage final : public StateStorage {
 public:
  void save_execution(const WorkflowExecution& ex) override;

  bool load_execution(const ExecutionId& id, WorkflowExecution& out) override;

  void clear();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<ExecutionId, std::string> blobs_;
};

}  // namespace tf
