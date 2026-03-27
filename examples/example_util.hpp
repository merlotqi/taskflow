// Small helpers shared by programs under examples/ (not part of the TaskFlow library API).
#pragma once

#include <chrono>
#include <iostream>
#include <optional>
#include <string_view>
#include <taskflow/task_manager.hpp>
#include <thread>
#include <vector>

namespace taskflow::examples {

inline void print_banner(std::string_view title) { std::cout << "TaskFlow Example: " << title << '\n'; }

// Blocks until the task is no longer created or running; returns the terminal state.
inline std::optional<TaskState> wait_until_inactive(TaskManager& manager, TaskID id,
                                                    std::chrono::milliseconds poll = std::chrono::milliseconds(50)) {
  while (true) {
    auto state = manager.query_state(id);
    if (state && *state != TaskState::running && *state != TaskState::created) {
      return state;
    }
    std::this_thread::sleep_for(poll);
  }
}

inline void wait_all_inactive(TaskManager& manager, const std::vector<TaskID>& ids,
                              std::chrono::milliseconds poll = std::chrono::milliseconds(50)) {
  while (true) {
    bool all_done = true;
    for (TaskID tid : ids) {
      auto state = manager.query_state(tid);
      if (!state || *state == TaskState::running || *state == TaskState::created) {
        all_done = false;
        break;
      }
    }
    if (all_done) {
      return;
    }
    std::this_thread::sleep_for(poll);
  }
}

}  // namespace taskflow::examples
