#include <taskflow/workflow/blueprint.hpp>

#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace tf {

void WorkflowBlueprint::add_node(NodeDef node) {
  if (index_.count(node.id)) {
    throw std::invalid_argument("duplicate node id: " + node.id);
  }
  index_[node.id] = nodes_.size();
  nodes_.push_back(std::move(node));
}

void WorkflowBlueprint::add_edge(EdgeDef edge) {
  if (!find_node(edge.from)) {
    throw std::invalid_argument("edge from unknown node: " + edge.from);
  }
  if (!find_node(edge.to)) {
    throw std::invalid_argument("edge to unknown node: " + edge.to);
  }
  edges_.push_back(std::move(edge));
}

const NodeDef* WorkflowBlueprint::find_node(const NodeId& id) const {
  auto it = index_.find(id);
  if (it == index_.end()) {
    return nullptr;
  }
  return &nodes_[it->second];
}

void WorkflowBlueprint::validate_acyclic() const {
  (void)topological_order();
}

std::vector<NodeId> WorkflowBlueprint::topological_order() const {
  std::unordered_map<NodeId, int> indegree;
  std::unordered_map<NodeId, std::vector<NodeId>> adj;

  for (const auto& n : nodes_) {
    indegree[n.id] = 0;
  }
  for (const auto& e : edges_) {
    adj[e.from].push_back(e.to);
    indegree[e.to]++;
  }

  std::queue<NodeId> q;
  for (const auto& n : nodes_) {
    if (indegree[n.id] == 0) {
      q.push(n.id);
    }
  }

  std::vector<NodeId> order;
  while (!q.empty()) {
    NodeId u = q.front();
    q.pop();
    order.push_back(u);
    for (const NodeId& v : adj[u]) {
      indegree[v]--;
      if (indegree[v] == 0) {
        q.push(v);
      }
    }
  }

  if (order.size() != nodes_.size()) {
    throw std::runtime_error("workflow blueprint has a cycle or disconnected nodes");
  }
  return order;
}

}  // namespace tf
