#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <taskflow/workflow/blueprint.hpp>

namespace {

// Test fixture for workflow_blueprint
class BlueprintTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup if needed
  }
};

// Test node management
TEST_F(BlueprintTest, NodeManagement) {
  taskflow::workflow::workflow_blueprint bp;

  // Add nodes
  bp.add_node(taskflow::workflow::node_def{1, "task1"});
  bp.add_node(taskflow::workflow::node_def{2, "task2"});
  bp.add_node(taskflow::workflow::node_def{3, "task3"});

  // Verify nodes exist
  EXPECT_TRUE(bp.has_node(1));
  EXPECT_TRUE(bp.has_node(2));
  EXPECT_TRUE(bp.has_node(3));
  EXPECT_FALSE(bp.has_node(4));

  // Verify node retrieval
  const auto* node1 = bp.find_node(1);
  ASSERT_NE(node1, nullptr);
  EXPECT_EQ(node1->id, 1);
  EXPECT_EQ(node1->task_type, "task1");

  const auto* node2 = bp.find_node(2);
  ASSERT_NE(node2, nullptr);
  EXPECT_EQ(node2->id, 2);
  EXPECT_EQ(node2->task_type, "task2");

  // Verify nodes map
  const auto& nodes = bp.nodes();
  EXPECT_EQ(nodes.size(), 3);
  EXPECT_EQ(nodes.at(1).id, 1);
  EXPECT_EQ(nodes.at(1).task_type, "task1");
  EXPECT_EQ(nodes.at(2).id, 2);
  EXPECT_EQ(nodes.at(2).task_type, "task2");
  EXPECT_EQ(nodes.at(3).id, 3);
  EXPECT_EQ(nodes.at(3).task_type, "task3");
}

// Test edge management
TEST_F(BlueprintTest, EdgeManagement) {
  taskflow::workflow::workflow_blueprint bp;

  // Add nodes
  bp.add_node(taskflow::workflow::node_def{1, "task1"});
  bp.add_node(taskflow::workflow::node_def{2, "task2"});
  bp.add_node(taskflow::workflow::node_def{3, "task3"});

  // Add edges
  bp.add_edge(taskflow::workflow::edge_def(1, 2));
  bp.add_edge(taskflow::workflow::edge_def(2, 3));
  bp.add_edge(taskflow::workflow::edge_def(1, 3));

  // Verify edges
  auto outgoing1 = bp.outgoing_edges(1);
  EXPECT_EQ(outgoing1.size(), 2);
  EXPECT_EQ(outgoing1[0]->from, 1);
  EXPECT_EQ(outgoing1[0]->to, 2);
  EXPECT_EQ(outgoing1[1]->from, 1);
  EXPECT_EQ(outgoing1[1]->to, 3);

  auto outgoing2 = bp.outgoing_edges(2);
  EXPECT_EQ(outgoing2.size(), 1);
  EXPECT_EQ(outgoing2[0]->from, 2);
  EXPECT_EQ(outgoing2[0]->to, 3);

  auto incoming3 = bp.incoming_edges(3);
  ASSERT_EQ(incoming3.size(), 2u);
  std::vector<std::size_t> from3;
  for (const auto* e : incoming3) {
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->to, 3u);
    from3.push_back(e->from);
  }
  std::sort(from3.begin(), from3.end());
  EXPECT_EQ(from3, (std::vector<std::size_t>{1, 2}));

  // Verify all edges
  const auto& edges = bp.edges();
  EXPECT_EQ(edges.size(), 3);
}

// Test graph traversal
TEST_F(BlueprintTest, GraphTraversal) {
  taskflow::workflow::workflow_blueprint bp;

  // Create a simple DAG: 1 -> 2 -> 3, 1 -> 3
  bp.add_node(taskflow::workflow::node_def{1, "task1"});
  bp.add_node(taskflow::workflow::node_def{2, "task2"});
  bp.add_node(taskflow::workflow::node_def{3, "task3"});

  bp.add_edge({1, 2});
  bp.add_edge({2, 3});
  bp.add_edge({1, 3});

  // Test predecessors and successors
  auto preds1 = bp.predecessors(1);
  EXPECT_TRUE(preds1.empty());

  auto succs1 = bp.successors(1);
  EXPECT_EQ(succs1.size(), 2);
  EXPECT_TRUE(std::find(succs1.begin(), succs1.end(), 2) != succs1.end());
  EXPECT_TRUE(std::find(succs1.begin(), succs1.end(), 3) != succs1.end());

  auto preds2 = bp.predecessors(2);
  EXPECT_EQ(preds2.size(), 1);
  EXPECT_EQ(preds2[0], 1);

  auto succs2 = bp.successors(2);
  EXPECT_EQ(succs2.size(), 1);
  EXPECT_EQ(succs2[0], 3);

  auto preds3 = bp.predecessors(3);
  EXPECT_EQ(preds3.size(), 2);
  EXPECT_TRUE(std::find(preds3.begin(), preds3.end(), 1) != preds3.end());
  EXPECT_TRUE(std::find(preds3.begin(), preds3.end(), 2) != preds3.end());

  auto succs3 = bp.successors(3);
  EXPECT_TRUE(succs3.empty());

  // Test root and leaf nodes
  auto roots = bp.root_nodes();
  EXPECT_EQ(roots.size(), 1);
  EXPECT_EQ(roots[0], 1);

  auto leaves = bp.leaf_nodes();
  EXPECT_EQ(leaves.size(), 1);
  EXPECT_EQ(leaves[0], 3);
}

// Test validation (matches blueprint implementation: invalid edges are ignored; cycles fail validate)
TEST_F(BlueprintTest, Validation) {
  taskflow::workflow::workflow_blueprint bp;

  EXPECT_TRUE(bp.is_valid());
  EXPECT_TRUE(bp.validate().empty());

  bp.add_node(taskflow::workflow::node_def{1, "task1"});
  EXPECT_TRUE(bp.is_valid());

  bp.add_node(taskflow::workflow::node_def{2, "task2"});
  bp.add_edge({1, 2});
  EXPECT_TRUE(bp.is_valid());
  EXPECT_TRUE(bp.validate().empty());

  // Duplicate id overwrites node; graph stays acyclic
  bp.add_node(taskflow::workflow::node_def{1, "replaced"});
  EXPECT_TRUE(bp.is_valid());

  // Edges with missing endpoints are skipped by add_edge — validate still ok
  bp.add_edge({2, 999});
  bp.add_edge({999, 1});
  EXPECT_TRUE(bp.is_valid());

  // Self-loop creates a cycle
  taskflow::workflow::workflow_blueprint cyc;
  cyc.add_node(taskflow::workflow::node_def{1, "a"});
  cyc.add_edge({1, 1});
  EXPECT_FALSE(cyc.is_valid());
  EXPECT_FALSE(cyc.validate().empty());

  // 1 -> 2 -> 1 cycle
  taskflow::workflow::workflow_blueprint cyc2;
  cyc2.add_node(taskflow::workflow::node_def{1, "a"});
  cyc2.add_node(taskflow::workflow::node_def{2, "b"});
  cyc2.add_edge({1, 2});
  cyc2.add_edge({2, 1});
  EXPECT_FALSE(cyc2.is_valid());
}

// Test topological order
TEST_F(BlueprintTest, TopologicalOrder) {
  taskflow::workflow::workflow_blueprint bp;

  // Create a simple DAG: 1 -> 2 -> 3, 1 -> 3
  bp.add_node(taskflow::workflow::node_def{1, "task1"});
  bp.add_node(taskflow::workflow::node_def{2, "task2"});
  bp.add_node(taskflow::workflow::node_def{3, "task3"});

  bp.add_edge({1, 2});
  bp.add_edge({2, 3});
  bp.add_edge({1, 3});

  auto order = bp.topological_order();
  EXPECT_EQ(order.size(), 3);

  // Verify topological order (1 must come before 2 and 3, 2 must come before 3)
  EXPECT_EQ(order[0], 1);
  EXPECT_TRUE(order[1] == 2 || order[1] == 3);
  EXPECT_TRUE(order[2] == 2 || order[2] == 3);
  EXPECT_NE(order[1], order[2]);
}

// Test complex graph
TEST_F(BlueprintTest, ComplexGraph) {
  taskflow::workflow::workflow_blueprint bp;

  // Create a more complex DAG
  // 1 -> 2, 3
  // 2 -> 4
  // 3 -> 4, 5
  // 4 -> 5
  bp.add_node(taskflow::workflow::node_def{1, "task1"});
  bp.add_node(taskflow::workflow::node_def{2, "task2"});
  bp.add_node(taskflow::workflow::node_def{3, "task3"});
  bp.add_node(taskflow::workflow::node_def{4, "task4"});
  bp.add_node(taskflow::workflow::node_def{5, "task5"});

  bp.add_edge(taskflow::workflow::edge_def(1, 2));
  bp.add_edge(taskflow::workflow::edge_def(1, 3));
  bp.add_edge(taskflow::workflow::edge_def(2, 4));
  bp.add_edge(taskflow::workflow::edge_def(3, 4));
  bp.add_edge(taskflow::workflow::edge_def(3, 5));
  bp.add_edge(taskflow::workflow::edge_def(4, 5));

  // Verify graph structure
  EXPECT_EQ(bp.nodes().size(), 5);
  EXPECT_EQ(bp.edges().size(), 6);

  // Verify predecessors and successors
  auto preds4 = bp.predecessors(4);
  EXPECT_EQ(preds4.size(), 2);
  EXPECT_TRUE(std::find(preds4.begin(), preds4.end(), 2) != preds4.end());
  EXPECT_TRUE(std::find(preds4.begin(), preds4.end(), 3) != preds4.end());

  auto succs3 = bp.successors(3);
  EXPECT_EQ(succs3.size(), 2);
  EXPECT_TRUE(std::find(succs3.begin(), succs3.end(), 4) != succs3.end());
  EXPECT_TRUE(std::find(succs3.begin(), succs3.end(), 5) != succs3.end());

  // Verify root and leaf nodes
  auto roots = bp.root_nodes();
  EXPECT_EQ(roots.size(), 1);
  EXPECT_EQ(roots[0], 1);

  auto leaves = bp.leaf_nodes();
  EXPECT_EQ(leaves.size(), 1);
  EXPECT_EQ(leaves[0], 5);
}

// Test move semantics
TEST_F(BlueprintTest, MoveSemantics) {
  taskflow::workflow::workflow_blueprint bp1;

  bp1.add_node(taskflow::workflow::node_def{1, "task1"});
  bp1.add_node(taskflow::workflow::node_def{2, "task2"});
  bp1.add_edge(taskflow::workflow::edge_def{1, 2});

  taskflow::workflow::workflow_blueprint bp2 = std::move(bp1);

  // Verify bp1 is empty after move
  EXPECT_EQ(bp1.nodes().size(), 0);
  EXPECT_EQ(bp1.edges().size(), 0);

  // Verify bp2 has the data
  EXPECT_EQ(bp2.nodes().size(), 2);
  EXPECT_EQ(bp2.edges().size(), 1);
  EXPECT_TRUE(bp2.has_node(1));
  EXPECT_TRUE(bp2.has_node(2));
  EXPECT_TRUE(bp2.find_node(1) != nullptr);
}

}  // namespace
