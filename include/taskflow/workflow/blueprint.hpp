#pragma once

#include <taskflow/workflow/edge.hpp>
#include <taskflow/workflow/node.hpp>

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf {

class WorkflowBlueprint {
 public:
  void add_node(NodeDef node);
  void add_edge(EdgeDef edge);

  const std::vector<NodeDef>& nodes() const { return nodes_; }
  const std::vector<EdgeDef>& edges() const { return edges_; }

  const NodeDef* find_node(const NodeId& id) const;

  std::vector<NodeId> topological_order() const;

  void validate_acyclic() const;

 private:
  std::vector<NodeDef> nodes_;
  std::vector<EdgeDef> edges_;
  std::unordered_map<NodeId, std::size_t> index_;
};

}  // namespace tf
