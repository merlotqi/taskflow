#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "parallel_executor.hpp"

namespace taskflow::engine {

class default_thread_pool : public parallel_executor {
 public:
  explicit default_thread_pool(std::size_t threads = std::thread::hardware_concurrency());
  ~default_thread_pool() override;

  default_thread_pool(const default_thread_pool&) = delete;
  default_thread_pool& operator=(const default_thread_pool&) = delete;

  void submit(std::function<void()> task) override;
  void wait_all() override;
  [[nodiscard]] std::size_t parallelism() const override;

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable completed_cv_;
  std::size_t active_tasks_ = 0;
  bool stop_ = false;
};

}  // namespace taskflow::engine
