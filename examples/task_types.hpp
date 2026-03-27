#pragma once

#include <chrono>
#include <iostream>
#include <taskflow/task_manager.hpp>
#include <thread>

// Example task types with different observability levels
struct NoObservationTask {
  static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::none;
  static constexpr bool cancellable = false;

  void operator()(taskflow::TaskCtx& ctx) const {
    std::cout << "No observation task executing" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx.success();
  }
};

struct BasicObservationTask {
  static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::basic;
  static constexpr bool cancellable = false;

  void operator()(taskflow::TaskCtx& ctx) const {
    std::cout << "Basic observation task executing" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx.success();
  }
};

struct ProgressObservationTask {
  static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::progress;
  static constexpr bool cancellable = false;

  void operator()(taskflow::TaskCtx& ctx) const {
    std::cout << "Progress observation task executing" << std::endl;
    ctx.report_progress(0.5f, "Halfway done");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx.success();
  }
};

struct CancellableTask {
  static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::basic;
  static constexpr bool cancellable = true;

  void operator()(taskflow::TaskCtx& ctx) const {
    std::cout << "Cancellable task executing" << std::endl;
    for (int i = 0; i < 200 && !ctx.is_cancelled(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!ctx.is_cancelled()) {
      ctx.success();
    }
  }
};

// Trivially copyable progress blob for report_progress / get_progress<CustomProgress>
struct CustomProgress {
  int current_step{0};
  int total_steps{0};
  char phase[32]{};
  double percentage{0.0};
};

struct CustomProgressTask {
  static constexpr bool progress_info = true;

  void operator()(taskflow::TaskCtx& ctx) const {
    std::cout << "Custom progress task executing" << std::endl;

    ctx.report_progress(CustomProgress{10, 100, "starting", 10.0});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ctx.report_progress(CustomProgress{50, 100, "processing", 50.0});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ctx.report_progress(CustomProgress{100, 100, "completed", 100.0});
    ctx.success();
  }
};
