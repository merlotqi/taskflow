#include "taskflow/engine/registry.hpp"

namespace taskflow::engine {

void task_registry::register_task(std::string type_name, core::task_factory factory) {
  factories_[std::move(type_name)] = std::move(factory);
}

core::task_wrapper task_registry::create(const std::string& type_name) const {
  auto it = factories_.find(type_name);
  if (it == factories_.end() || !it->second) return core::task_wrapper{};
  return it->second();
}

bool task_registry::has_task(const std::string& type_name) const noexcept {
  return factories_.find(type_name) != factories_.end();
}

std::vector<std::string> task_registry::registered_types() const {
  std::vector<std::string> types;
  types.reserve(factories_.size());
  for (const auto& [name, _] : factories_) types.push_back(name);
  return types;
}

}  // namespace taskflow::engine
