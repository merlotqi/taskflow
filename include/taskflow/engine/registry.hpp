#pragma once

#include <taskflow/core/task.hpp>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace tf {

using TaskFactoryFn = std::function<TaskPtr()>;

class TaskRegistry {
 public:
  void register_type(std::string type, TaskFactoryFn factory);

  TaskPtr create(const std::string& type) const;

  bool contains(const std::string& type) const;

 private:
  std::unordered_map<std::string, TaskFactoryFn> factories_;
};

}  // namespace tf
