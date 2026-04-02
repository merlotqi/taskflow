#pragma once

#include <taskflow/core/task_ctx.hpp>
#include <memory>

namespace tf {

struct TaskBase {
  virtual ~TaskBase() = default;
  virtual void execute(TaskCtx& ctx) = 0;
};

using TaskPtr = std::unique_ptr<TaskBase>;
using TaskFactory = TaskPtr (*)();

}  // namespace tf
