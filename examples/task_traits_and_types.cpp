#include <iostream>
#include <taskflow/task_manager.hpp>
#include <thread>
#include <utility>

#include "example_util.hpp"
#include "task_types.hpp"

int main() {
  using namespace taskflow::examples;

  print_banner("Task traits and functor task types");

  auto& manager = taskflow::TaskManager::getInstance();
  manager.start_processing(4);

  auto run_and_wait = [&](auto&& task, const char* label) {
    auto id = manager.submit_task(std::forward<decltype(task)>(task));
    std::cout << label << " id=" << id << std::endl;
    if (auto st = wait_until_inactive(manager, id)) {
      std::cout << "  terminal state: " << static_cast<int>(*st) << std::endl;
    }
  };

  run_and_wait(NoObservationTask{}, "NoObservationTask");
  run_and_wait(BasicObservationTask{}, "BasicObservationTask");
  run_and_wait(ProgressObservationTask{}, "ProgressObservationTask");

  auto cancel_id = manager.submit_task(CancellableTask{});
  std::cout << "CancellableTask id=" << cancel_id << " (cancelling...)" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  manager.cancel_task(cancel_id);
  if (auto st = wait_until_inactive(manager, cancel_id)) {
    std::cout << "  terminal state: " << static_cast<int>(*st) << std::endl;
    if (auto err = manager.get_error(cancel_id)) {
      std::cout << "  error: " << *err << std::endl;
    }
  }

  auto cp_id = manager.submit_task(CustomProgressTask{});
  std::cout << "CustomProgressTask id=" << cp_id << std::endl;
  while (true) {
    auto state = manager.query_state(cp_id);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "  terminal state: " << static_cast<int>(*state) << std::endl;
      break;
    }
    if (auto p = manager.get_progress<CustomProgress>(cp_id)) {
      std::cout << "  progress: step " << p->current_step << "/" << p->total_steps << " phase=" << p->phase << " "
                << p->percentage << "%" << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }

  manager.stop_processing();
  std::cout << "Example completed!" << std::endl;
  return 0;
}
