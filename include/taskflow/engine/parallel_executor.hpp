#pragma once

#include <cstddef>
#include <functional>

namespace taskflow::engine {

class parallel_executor {
 public:
  virtual ~parallel_executor() = default;

  virtual void submit(std::function<void()> task) = 0;
  virtual void wait_all() = 0;
  [[nodiscard]] virtual std::size_t parallelism() const = 0;
};

}  // namespace taskflow::engine
