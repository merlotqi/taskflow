#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "taskflow/core/task.hpp"

namespace taskflow::engine {

/// Abstract registry for tests and alternate backends; executor accepts `const itask_registry&`.
class itask_registry {
 public:
  virtual ~itask_registry() = default;

  [[nodiscard]] virtual core::task_wrapper create(const std::string& type_name) const = 0;
  [[nodiscard]] virtual bool has_task(const std::string& type_name) const noexcept = 0;
};

class task_registry final : public itask_registry {
 public:
  task_registry() = default;

  void register_task(std::string type_name, core::task_factory factory);

  template <typename T, std::enable_if_t<core::is_task_v<T>, int> = 0>
  void register_task(std::string type_name) {
    register_task(std::move(type_name), []() { return core::make_task<T>(); });
  }

  [[nodiscard]] core::task_wrapper create(const std::string& type_name) const override;
  [[nodiscard]] bool has_task(const std::string& type_name) const noexcept override;
  [[nodiscard]] std::vector<std::string> registered_types() const;

 private:
  std::unordered_map<std::string, core::task_factory> factories_;
};

}  // namespace taskflow::engine
