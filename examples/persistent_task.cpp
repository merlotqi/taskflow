#include <iostream>
#include <taskflow/task_manager.hpp>

#include "example_util.hpp"

int main() {
  using namespace taskflow::examples;

  print_banner("Persistent Task Reawakening");

  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(4);

  static int reawaken_count = 0;

  auto persistent_task = [](taskflow::TaskCtx& ctx) {
    std::cout << "Persistent task " << ctx.id << " executing (count: " << ++reawaken_count << ")" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    nlohmann::json result = {{"execution_count", reawaken_count}, {"task_type", "persistent"}};
    ctx.success_with_result(taskflow::ResultPayload::json(result));
  };

  auto persistent_id = manager.submit_task(persistent_task, taskflow::TaskLifecycle::persistent);
  std::cout << "Submitted persistent task: " << persistent_id << std::endl;
  std::cout << "Is persistent: " << (manager.is_persistent_task(persistent_id) ? "Yes" : "No") << std::endl;

  wait_until_inactive(manager, persistent_id);
  std::cout << "Persistent task first execution completed" << std::endl;
  if (auto result = manager.get_result(persistent_id)) {
    std::cout << "First execution result: " << result->data.json_data.dump() << std::endl;
  }

  std::cout << "Reawakening persistent task..." << std::endl;
  auto new_persistent_task = [](taskflow::TaskCtx& ctx) {
    std::cout << "Reawakened persistent task " << ctx.id << " with new logic!" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    nlohmann::json result = {{"execution_count", 2},
                             {"task_type", "persistent_reawakened"},
                             {"message", "This is the reawakened execution"}};
    ctx.success_with_result(taskflow::ResultPayload::json(result));
  };

  bool reawaken_success = manager.reawaken_task(persistent_id, new_persistent_task);
  std::cout << "Reawaken request: " << (reawaken_success ? "Success" : "Failed") << std::endl;

  wait_until_inactive(manager, persistent_id);
  std::cout << "Persistent task reawakened execution completed" << std::endl;
  if (auto result = manager.get_result(persistent_id)) {
    std::cout << "Reawakened execution result: " << result->data.json_data.dump() << std::endl;
  }

  manager.stop_processing();
  std::cout << "Example completed!" << std::endl;
  return 0;
}
