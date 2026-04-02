#include <taskflow/engine/registry.hpp>

namespace tf {

void TaskRegistry::register_type(std::string type, TaskFactoryFn factory) {
  factories_[std::move(type)] = std::move(factory);
}

TaskPtr TaskRegistry::create(const std::string& type) const {
  auto it = factories_.find(type);
  if (it == factories_.end()) {
    throw std::runtime_error("unknown task type: " + type);
  }
  return it->second();
}

bool TaskRegistry::contains(const std::string& type) const {
  return factories_.count(type) != 0;
}

}  // namespace tf
