#include <iostream>
#include <taskflow/task_manager.hpp>
#include <vector>

#include "example_util.hpp"

int main() {
  using namespace taskflow::examples;

  print_banner("Multiple Tasks");

  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(4);

  std::vector<taskflow::TaskID> task_ids;
  for (int i = 0; i < 3; ++i) {
    auto task = [i](taskflow::TaskCtx& ctx) {
      std::cout << "Task " << i << " (ID: " << ctx.id << ") executing" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(50 + i * 25));
      ctx.success();
    };
    auto id = manager.submit_task(task);
    task_ids.push_back(id);
    std::cout << "Submitted task " << i << ": " << id << std::endl;
  }

  wait_all_inactive(manager, task_ids);
  std::cout << "All tasks completed" << std::endl;

  manager.stop_processing();
  std::cout << "Example completed!" << std::endl;
  return 0;
}
