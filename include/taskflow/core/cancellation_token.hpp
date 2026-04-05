#pragma once

#include <atomic>
#include <memory>

namespace taskflow::core {

class cancellation_token {
 public:
  cancellation_token() : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}

  void cancel() { cancelled_->store(true); }

  [[nodiscard]] bool is_cancelled() const { return cancelled_->load(); }

  void reset() { cancelled_->store(false); }

 private:
  std::shared_ptr<std::atomic<bool>> cancelled_;
};

class cancellation_source {
 public:
  cancellation_source() : token_(std::make_shared<cancellation_token>()) {}

  [[nodiscard]] cancellation_token token() const { return *token_; }

  void cancel() { token_->cancel(); }

  [[nodiscard]] bool is_cancelled() const { return token_->is_cancelled(); }

 private:
  std::shared_ptr<cancellation_token> token_;
};

}  // namespace taskflow::core
