#include <taskflow/workflow/serializer.hpp>

#include <stdexcept>

namespace tf {

namespace {

const char* task_state_str(TaskState s) {
  switch (s) {
    case TaskState::Pending:
      return "pending";
    case TaskState::Running:
      return "running";
    case TaskState::Success:
      return "success";
    case TaskState::Failed:
      return "failed";
  }
  return "pending";
}

TaskState task_state_from_str(const std::string& s) {
  if (s == "pending") {
    return TaskState::Pending;
  }
  if (s == "running") {
    return TaskState::Running;
  }
  if (s == "success") {
    return TaskState::Success;
  }
  if (s == "failed") {
    return TaskState::Failed;
  }
  throw std::invalid_argument("unknown task state: " + s);
}

}  // namespace

nlohmann::json blueprint_to_json(const WorkflowBlueprint& bp) {
  nlohmann::json j;
  j["nodes"] = nlohmann::json::array();
  for (const auto& n : bp.nodes()) {
    j["nodes"].push_back({{"id", n.id}, {"task_type", n.task_type}, {"max_retries", n.max_retries}});
  }
  j["edges"] = nlohmann::json::array();
  for (const auto& e : bp.edges()) {
    j["edges"].push_back({{"from", e.from}, {"to", e.to}, {"condition", e.condition}});
  }
  return j;
}

WorkflowBlueprint blueprint_from_json(const nlohmann::json& j) {
  WorkflowBlueprint bp;
  for (const auto& item : j.at("nodes")) {
    NodeDef n;
    n.id = item.at("id").get<std::string>();
    n.task_type = item.at("task_type").get<std::string>();
    n.max_retries = item.value("max_retries", 0);
    bp.add_node(std::move(n));
  }
  for (const auto& item : j.at("edges")) {
    EdgeDef e;
    e.from = item.at("from").get<std::string>();
    e.to = item.at("to").get<std::string>();
    e.condition = item.value("condition", std::string{});
    bp.add_edge(std::move(e));
  }
  return bp;
}

std::string blueprint_to_string(const WorkflowBlueprint& bp) {
  return blueprint_to_json(bp).dump();
}

WorkflowBlueprint blueprint_from_string(const std::string& s) {
  return blueprint_from_json(nlohmann::json::parse(s));
}

nlohmann::json execution_to_json(const WorkflowExecution& ex) {
  nlohmann::json j;
  j["id"] = ex.id;
  j["blueprint"] = blueprint_to_json(ex.blueprint);
  j["node_states"] = nlohmann::json::object();
  for (const auto& [nid, st] : ex.node_states) {
    j["node_states"][nid] = task_state_str(st);
  }
  j["context"] = ex.context.bus();
  j["retry_count"] = nlohmann::json::object();
  for (const auto& [nid, c] : ex.retry_count) {
    j["retry_count"][nid] = c;
  }
  return j;
}

void execution_from_json(const nlohmann::json& j, WorkflowExecution& ex) {
  ex.id = j.at("id").get<std::string>();
  ex.blueprint = blueprint_from_json(j.at("blueprint"));
  ex.node_states.clear();
  for (auto it = j.at("node_states").begin(); it != j.at("node_states").end(); ++it) {
    ex.node_states[it.key()] = task_state_from_str(it.value().get<std::string>());
  }
  ex.context.bus() = j.at("context");
  ex.retry_count.clear();
  if (j.contains("retry_count")) {
    for (auto it = j.at("retry_count").begin(); it != j.at("retry_count").end(); ++it) {
      ex.retry_count[it.key()] = it.value().get<int>();
    }
  }
}

}  // namespace tf
