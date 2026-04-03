#include <nlohmann/json.hpp>

#include "taskflow/engine/execution.hpp"
#include "taskflow/workflow/serializer.hpp"

namespace taskflow::engine {

std::string workflow_execution::to_snapshot_json() const {
  std::lock_guard<std::mutex> lock(*state_mutex_);

  nlohmann::json j;
  j["exec_id"] = exec_id_;
  j["start_time"] = start_time_;
  j["end_time"] = end_time_;

  if (blueprint_) {
    j["blueprint_json"] = workflow::serializer::to_json(*blueprint_);
  }

  j["nodes"] = nlohmann::json::array();
  for (const auto& [nid, ns] : node_states_) {
    nlohmann::json n;
    n["id"] = nid;
    n["state"] = std::string(core::to_string(ns.state));
    n["retry_count"] = ns.retry_count;
    n["error"] = ns.error_message;
    j["nodes"].push_back(std::move(n));
  }

  j["completed_keys"] = nlohmann::json::array();
  for (const auto& k : completed_nodes_) {
    j["completed_keys"].push_back(nlohmann::json::object({{"exec_id", k.exec_id}, {"node_id", k.node_id}}));
  }

  return j.dump();
}

}  // namespace taskflow::engine
