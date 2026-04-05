#pragma once

#include <cstddef>
#include <functional>

#include "parallel_executor.hpp"

namespace taskflow::engine {

class sync_executor : public parallel_executor {
 public:
  sync_executor() = default;

  void submit(std::function<void()> task) override {
    if (task) task();
  }

  void wait_all() override {}
  [[nodiscard]] std::size_t parallelism() const override { return 1; }
};

}  // namespace taskflow::engine
