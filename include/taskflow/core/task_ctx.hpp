#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace tf {

class TaskCtx {
 public:
  nlohmann::json& bus() { return bus_; }
  const nlohmann::json& bus() const { return bus_; }

  void set(std::string key, nlohmann::json value) {
    bus_[std::move(key)] = std::move(value);
  }

  nlohmann::json get(const std::string& key) const {
    auto it = bus_.find(key);
    if (it == bus_.end()) {
      return nlohmann::json();
    }
    return it.value();
  }

  void report_progress(double p) { progress_ = p; }
  double progress() const { return progress_; }

  void cancel() { cancelled_ = true; }
  bool is_cancelled() const { return cancelled_; }

 private:
  nlohmann::json bus_;
  double progress_{0};
  bool cancelled_{false};
};

}  // namespace tf
