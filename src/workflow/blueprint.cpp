#include "taskflow/workflow/blueprint.hpp"

#include <queue>

namespace taskflow::workflow {

void workflow_blueprint::add_node(node_def node) {
  nodes_[node.id] = std::move(node);
  rebuild_adjacency();
}

const node_def* workflow_blueprint::find_node(std::size_t id) const noexcept {
  auto it = nodes_.find(id);
  return it != nodes_.end() ? &it->second : nullptr;
}

bool workflow_blueprint::has_node(std::size_t id) const noexcept { return nodes_.find(id) != nodes_.end(); }

const std::unordered_map<std::size_t, node_def>& workflow_blueprint::nodes() const noexcept { return nodes_; }

void workflow_blueprint::add_edge(edge_def edge) {
  if (!has_node(edge.from) || !has_node(edge.to)) return;
  edges_.push_back(std::move(edge));
  rebuild_adjacency();
}

std::vector<const edge_def*> workflow_blueprint::outgoing_edges(std::size_t from_id) const {
  std::vector<const edge_def*> result;
  for (const auto& e : edges_) {
    if (e.from == from_id) result.push_back(&e);
  }
  return result;
}

std::vector<const edge_def*> workflow_blueprint::incoming_edges(std::size_t to_id) const {
  std::vector<const edge_def*> result;
  for (const auto& e : edges_) {
    if (e.to == to_id) result.push_back(&e);
  }
  return result;
}

const std::vector<edge_def>& workflow_blueprint::edges() const noexcept { return edges_; }

std::string workflow_blueprint::validate() const {
  for (const auto& e : edges_) {
    if (!has_node(e.from)) return "edge from=" + std::to_string(e.from) + ": source node not found";
    if (!has_node(e.to)) return "edge to=" + std::to_string(e.to) + ": destination node not found";
  }
  std::unordered_map<std::size_t, int> in_degree;
  for (const auto& [id, _] : nodes_) in_degree[id] = 0;
  for (const auto& e : edges_) in_degree[e.to]++;
  std::queue<std::size_t> q;
  for (const auto& [id, deg] : in_degree)
    if (deg == 0) q.push(id);
  std::size_t visited = 0;
  while (!q.empty()) {
    auto cur = q.front();
    q.pop();
    visited++;
    auto it = adjacency_.find(cur);
    if (it != adjacency_.end()) {
      for (auto succ : it->second)
        if (--in_degree[succ] == 0) q.push(succ);
    }
  }
  if (visited != nodes_.size())
    return "cycle detected: visited " + std::to_string(visited) + "/" + std::to_string(nodes_.size()) + " nodes";
  return "";
}

bool workflow_blueprint::is_valid() const { return validate().empty(); }

std::vector<std::size_t> workflow_blueprint::topological_order() const {
  std::unordered_map<std::size_t, int> in_degree;
  for (const auto& [id, _] : nodes_) in_degree[id] = 0;
  for (const auto& e : edges_) in_degree[e.to]++;
  std::queue<std::size_t> q;
  for (const auto& [id, deg] : in_degree)
    if (deg == 0) q.push(id);
  std::vector<std::size_t> order;
  order.reserve(nodes_.size());
  while (!q.empty()) {
    auto cur = q.front();
    q.pop();
    order.push_back(cur);
    auto it = adjacency_.find(cur);
    if (it != adjacency_.end())
      for (auto succ : it->second)
        if (--in_degree[succ] == 0) q.push(succ);
  }
  return order;
}

std::vector<std::size_t> workflow_blueprint::predecessors(std::size_t node_id) const {
  auto it = reverse_adj_.find(node_id);
  return it != reverse_adj_.end() ? it->second : std::vector<std::size_t>{};
}

std::vector<std::size_t> workflow_blueprint::successors(std::size_t node_id) const {
  auto it = adjacency_.find(node_id);
  return it != adjacency_.end() ? it->second : std::vector<std::size_t>{};
}

std::vector<std::size_t> workflow_blueprint::root_nodes() const {
  std::vector<std::size_t> roots;
  for (const auto& [id, _] : nodes_)
    if (predecessors(id).empty()) roots.push_back(id);
  return roots;
}

std::vector<std::size_t> workflow_blueprint::leaf_nodes() const {
  std::vector<std::size_t> leaves;
  for (const auto& [id, _] : nodes_)
    if (successors(id).empty()) leaves.push_back(id);
  return leaves;
}

void workflow_blueprint::rebuild_adjacency() {
  adjacency_.clear();
  reverse_adj_.clear();
  for (const auto& [id, _] : nodes_) {
    adjacency_[id] = {};
    reverse_adj_[id] = {};
  }
  for (const auto& e : edges_) {
    adjacency_[e.from].push_back(e.to);
    reverse_adj_[e.to].push_back(e.from);
  }
}

}  // namespace taskflow::workflow
