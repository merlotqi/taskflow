#pragma once

#include <taskflow/core/types.hpp>

#include <string>

namespace tf {

class Observer {
 public:
  virtual ~Observer() = default;

  virtual void on_task_start(const ExecutionId& exec, const NodeId& node) {
    (void)exec;
    (void)node;
  }
  virtual void on_task_complete(const ExecutionId& exec, const NodeId& node) {
    (void)exec;
    (void)node;
  }
  virtual void on_task_fail(const ExecutionId& exec, const NodeId& node,
                            const std::string& message) {
    (void)exec;
    (void)node;
    (void)message;
  }
};

}  // namespace tf
