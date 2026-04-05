#include "taskflow/engine/default_thread_pool.hpp"

namespace taskflow::engine {

default_thread_pool::default_thread_pool(std::size_t threads) {
  if (threads == 0) threads = 1;
  workers_.reserve(threads);
  for (std::size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this] {
      for (;;) {
        std::function<void()> task;
        {
          std::unique_lock lock(mutex_);
          cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
          if (stop_ && tasks_.empty()) return;
          task = std::move(tasks_.front());
          tasks_.pop();
          ++active_tasks_;
        }
        if (task) task();
        {
          std::unique_lock lock(mutex_);
          --active_tasks_;
          if (tasks_.empty() && active_tasks_ == 0) {
            completed_cv_.notify_all();
          }
        }
      }
    });
  }
}

default_thread_pool::~default_thread_pool() {
  {
    std::unique_lock lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& w : workers_) {
    if (w.joinable()) w.join();
  }
}

void default_thread_pool::submit(std::function<void()> task) {
  {
    std::unique_lock lock(mutex_);
    tasks_.push(std::move(task));
  }
  cv_.notify_one();
}

void default_thread_pool::wait_all() {
  std::unique_lock lock(mutex_);
  completed_cv_.wait(lock, [this] { return tasks_.empty() && active_tasks_ == 0; });
}

std::size_t default_thread_pool::parallelism() const { return workers_.size(); }

}  // namespace taskflow::engine
