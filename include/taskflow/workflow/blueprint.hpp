#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "edge.hpp"
#include "node.hpp"

namespace taskflow::workflow {

class workflow_blueprint {
 public:
  workflow_blueprint() = default;
  workflow_blueprint(const workflow_blueprint&) = delete;
  workflow_blueprint& operator=(const workflow_blueprint&) = delete;
  workflow_blueprint(workflow_blueprint&&) noexcept = default;
  workflow_blueprint& operator=(workflow_blueprint&&) noexcept = default;

  void add_node(node_def node);
  [[nodiscard]] const node_def* find_node(std::size_t id) const noexcept;
  [[nodiscard]] bool has_node(std::size_t id) const noexcept;
  [[nodiscard]] const std::unordered_map<std::size_t, node_def>& nodes() const noexcept;

  void add_edge(edge_def edge);
  [[nodiscard]] std::vector<const edge_def*> outgoing_edges(std::size_t from_id) const;
  [[nodiscard]] std::vector<const edge_def*> incoming_edges(std::size_t to_id) const;
  [[nodiscard]] const std::vector<edge_def>& edges() const noexcept;

  [[nodiscard]] std::string validate() const;
  [[nodiscard]] bool is_valid() const;
  [[nodiscard]] std::vector<std::size_t> topological_order() const;

  [[nodiscard]] std::vector<std::size_t> predecessors(std::size_t node_id) const;
  [[nodiscard]] std::vector<std::size_t> successors(std::size_t node_id) const;
  [[nodiscard]] std::vector<std::size_t> root_nodes() const;
  [[nodiscard]] std::vector<std::size_t> leaf_nodes() const;

 private:
  std::unordered_map<std::size_t, node_def> nodes_;
  std::vector<edge_def> edges_;
  std::unordered_map<std::size_t, std::vector<std::size_t>> adjacency_;
  std::unordered_map<std::size_t, std::vector<std::size_t>> reverse_adj_;
  void rebuild_adjacency();
};

}  // namespace taskflow::workflow
