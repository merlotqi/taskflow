#include "taskflow/workflow/serializer.hpp"

#include <nlohmann/json.hpp>

namespace taskflow::workflow {

using json = nlohmann::json;

std::string serializer::to_json(const workflow_blueprint& bp) {
  json j;

  // Serialize nodes
  j["nodes"] = json::array();
  for (const auto& [id, node] : bp.nodes()) {
    json node_json;
    node_json["id"] = node.id;
    node_json["task_type"] = node.task_type;
    if (node.label) {
      node_json["label"] = *node.label;
    }
    if (node.retry) {
      node_json["retry"] = {{"max_attempts", node.retry->max_attempts},
                            {"initial_delay_ms", node.retry->initial_delay_ms},
                            {"backoff_multiplier", node.retry->backoff_multiplier}};
    }
    if (!node.tags.empty()) {
      node_json["tags"] = node.tags;
    }
    j["nodes"].push_back(node_json);
  }

  // Serialize edges (structural only; edge_def::condition is std::function and is omitted)
  j["edges"] = json::array();
  for (const auto& edge : bp.edges()) {
    json edge_json;
    edge_json["from"] = edge.from;
    edge_json["to"] = edge.to;
    j["edges"].push_back(edge_json);
  }

  return j.dump(2);
}

std::optional<workflow_blueprint> serializer::from_json(const std::string& json_str) {
  try {
    json j = json::parse(json_str);
    workflow_blueprint bp;

    // Deserialize nodes
    if (j.contains("nodes") && j["nodes"].is_array()) {
      for (const auto& node_json : j["nodes"]) {
        node_def node;
        node.id = node_json["id"];
        node.task_type = node_json["task_type"];
        if (node_json.contains("label")) {
          node.label = node_json["label"];
        }
        if (node_json.contains("retry")) {
          core::retry_policy retry;
          retry.max_attempts = node_json["retry"]["max_attempts"];
          retry.initial_delay_ms = node_json["retry"]["initial_delay_ms"];
          retry.backoff_multiplier = node_json["retry"]["backoff_multiplier"];
          node.retry = retry;
        }
        if (node_json.contains("tags")) {
          node.tags = node_json["tags"];
        }
        bp.add_node(std::move(node));
      }
    }

    // Deserialize edges
    if (j.contains("edges") && j["edges"].is_array()) {
      for (const auto& edge_json : j["edges"]) {
        edge_def edge;
        edge.from = edge_json["from"];
        edge.to = edge_json["to"];
        bp.add_edge(std::move(edge));
      }
    }

    return bp;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

std::vector<std::uint8_t> serializer::to_binary(const workflow_blueprint& bp) {
  std::string json_str = to_json(bp);
  return std::vector<std::uint8_t>(json_str.begin(), json_str.end());
}

std::optional<workflow_blueprint> serializer::from_binary(const std::vector<std::uint8_t>& data) {
  std::string json_str(data.begin(), data.end());
  return from_json(json_str);
}

}  // namespace taskflow::workflow
