#include <iostream>
#include <taskflow/task_manager.hpp>

#include "example_util.hpp"

int main() {
  using namespace taskflow::examples;

  print_banner("Task with Failure");

  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(4);

  auto failing_task = [](taskflow::TaskCtx& ctx) {
    std::cout << "Task " << ctx.id << " failing" << std::endl;
    ctx.failure("Simulated failure");
  };

  auto fail_id = manager.submit_task(failing_task);
  std::cout << "Submitted failing task: " << fail_id << std::endl;

  if (auto state = wait_until_inactive(manager, fail_id)) {
    std::cout << "Failing task completed with state: " << static_cast<int>(*state) << std::endl;
    if (auto error = manager.get_error(fail_id)) {
      std::cout << "Error message: " << *error << std::endl;
    }
  }

  manager.stop_processing();
  std::cout << "Example completed!" << std::endl;
  return 0;
}
