#include <chrono>
#include <iostream>
#include <taskflow/task_manager.hpp>

#include "example_util.hpp"

int main() {
  using namespace taskflow::examples;

  print_banner("Task with Custom Result Storage");

  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(4);

  auto result_task = [](taskflow::TaskCtx& ctx) {
    std::cout << "Task " << ctx.id << " storing custom result" << std::endl;

    nlohmann::json result = {{"task_id", ctx.id},
                             {"status", "completed"},
                             {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
                             {"data", {{"processed_items", 42}, {"success_rate", 0.95}}}};

    ctx.success_with_result(taskflow::ResultPayload::json(result));
  };

  auto result_id = manager.submit_task(result_task);
  std::cout << "Submitted result task: " << result_id << std::endl;

  if (auto state = wait_until_inactive(manager, result_id)) {
    std::cout << "Result task completed with state: " << static_cast<int>(*state) << std::endl;

    if (auto result = manager.get_result(result_id)) {
      if (result->kind == taskflow::ResultKind::json) {
        std::cout << "Result JSON: " << result->data.json_data.dump(2) << std::endl;
      }
    } else {
      std::cout << "No result found" << std::endl;
    }
  }

  manager.stop_processing();
  std::cout << "Example completed!" << std::endl;
  return 0;
}
