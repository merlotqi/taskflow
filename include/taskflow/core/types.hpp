#pragma once

#include <string>

namespace tf {

using TaskId = std::string;
using ExecutionId = std::string;
using NodeId = std::string;

enum class TaskState { Pending, Running, Success, Failed };

enum class TaskPriority { Normal, High };

}  // namespace tf
