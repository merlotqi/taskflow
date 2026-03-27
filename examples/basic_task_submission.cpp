#include <iostream>
#include <taskflow/task_manager.hpp>

#include "example_util.hpp"

int main() {
  using namespace taskflow::examples;

  print_banner("Basic Task Submission");

  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(4);

  auto task = [](taskflow::TaskCtx& ctx) {
    std::cout << "Executing task: " << ctx.id << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ctx.success();
  };

  auto id = manager.submit_task(task);
  std::cout << "Submitted task: " << id << std::endl;

  if (auto state = wait_until_inactive(manager, id)) {
    std::cout << "Task completed with state: " << static_cast<int>(*state) << std::endl;
  }

  manager.stop_processing();
  std::cout << "Example completed!" << std::endl;
  return 0;
}
