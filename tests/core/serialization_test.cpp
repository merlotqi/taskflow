#include <gtest/gtest.h>

#include <taskflow/workflow/serializer.hpp>

namespace {

// Test fixture for serializer
class SerializationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a sample blueprint for testing
    bp_.add_node(taskflow::workflow::node_def{1, "task1"});
    bp_.add_node(taskflow::workflow::node_def{2, "task2"});
    bp_.add_node(taskflow::workflow::node_def{3, "task3"});
    bp_.add_edge(taskflow::workflow::edge_def{1, 2});
    bp_.add_edge(taskflow::workflow::edge_def{2, 3});
    bp_.add_edge(taskflow::workflow::edge_def{1, 3});
  }

  taskflow::workflow::workflow_blueprint bp_;
};

// Test JSON serialization and deserialization
TEST_F(SerializationTest, JsonSerialization) {
  // Serialize to JSON
  std::string json = taskflow::workflow::serializer::to_json(bp_);
  ASSERT_FALSE(json.empty());

  // Deserialize from JSON
  auto deserialized = taskflow::workflow::serializer::from_json(json);
  ASSERT_TRUE(deserialized.has_value());

  // Verify deserialized blueprint
  const auto& deserialized_bp = deserialized.value();
  EXPECT_EQ(deserialized_bp.nodes().size(), 3);
  EXPECT_EQ(deserialized_bp.edges().size(), 3);

  // Verify nodes
  EXPECT_TRUE(deserialized_bp.has_node(1));
  EXPECT_TRUE(deserialized_bp.has_node(2));
  EXPECT_TRUE(deserialized_bp.has_node(3));

  const auto* node1 = deserialized_bp.find_node(1);
  ASSERT_NE(node1, nullptr);
  EXPECT_EQ(node1->id, 1);
  EXPECT_EQ(node1->task_type, "task1");

  // Verify edges
  auto outgoing1 = deserialized_bp.outgoing_edges(1);
  EXPECT_EQ(outgoing1.size(), 2);
  EXPECT_EQ(outgoing1[0]->from, 1);
  EXPECT_EQ(outgoing1[0]->to, 2);
  EXPECT_EQ(outgoing1[1]->from, 1);
  EXPECT_EQ(outgoing1[1]->to, 3);
}

// Test JSON round-trip
TEST_F(SerializationTest, JsonRoundTrip) {
  // Serialize and deserialize
  std::string json = taskflow::workflow::serializer::to_json(bp_);
  auto deserialized = taskflow::workflow::serializer::from_json(json);
  ASSERT_TRUE(deserialized.has_value());

  // Verify round-trip equality
  const auto& deserialized_bp = deserialized.value();
  EXPECT_EQ(deserialized_bp.nodes().size(), bp_.nodes().size());
  EXPECT_EQ(deserialized_bp.edges().size(), bp_.edges().size());

  // Verify nodes
  for (const auto& [id, node] : bp_.nodes()) {
    const auto* deserialized_node = deserialized_bp.find_node(id);
    ASSERT_NE(deserialized_node, nullptr);
    EXPECT_EQ(deserialized_node->id, node.id);
    EXPECT_EQ(deserialized_node->task_type, node.task_type);
  }

  // Verify edges
  EXPECT_EQ(deserialized_bp.edges().size(), bp_.edges().size());
}

// Test empty blueprint serialization
TEST_F(SerializationTest, EmptyBlueprint) {
  taskflow::workflow::workflow_blueprint empty_bp;

  // Serialize empty blueprint
  std::string json = taskflow::workflow::serializer::to_json(empty_bp);
  ASSERT_FALSE(json.empty());

  // Deserialize empty blueprint
  auto deserialized = taskflow::workflow::serializer::from_json(json);
  ASSERT_TRUE(deserialized.has_value());

  // Verify empty blueprint
  const auto& deserialized_bp = deserialized.value();
  EXPECT_EQ(deserialized_bp.nodes().size(), 0);
  EXPECT_EQ(deserialized_bp.edges().size(), 0);
}

// Test binary serialization and deserialization
TEST_F(SerializationTest, BinarySerialization) {
  // Serialize to binary
  std::vector<std::uint8_t> binary = taskflow::workflow::serializer::to_binary(bp_);
  ASSERT_FALSE(binary.empty());

  // Deserialize from binary
  auto deserialized = taskflow::workflow::serializer::from_binary(binary);
  ASSERT_TRUE(deserialized.has_value());

  // Verify deserialized blueprint
  const auto& deserialized_bp = deserialized.value();
  EXPECT_EQ(deserialized_bp.nodes().size(), 3);
  EXPECT_EQ(deserialized_bp.edges().size(), 3);

  // Verify nodes
  EXPECT_TRUE(deserialized_bp.has_node(1));
  EXPECT_TRUE(deserialized_bp.has_node(2));
  EXPECT_TRUE(deserialized_bp.has_node(3));
}

// Test binary round-trip
TEST_F(SerializationTest, BinaryRoundTrip) {
  // Serialize and deserialize
  std::vector<std::uint8_t> binary = taskflow::workflow::serializer::to_binary(bp_);
  auto deserialized = taskflow::workflow::serializer::from_binary(binary);
  ASSERT_TRUE(deserialized.has_value());

  // Verify round-trip equality
  const auto& deserialized_bp = deserialized.value();
  EXPECT_EQ(deserialized_bp.nodes().size(), bp_.nodes().size());
  EXPECT_EQ(deserialized_bp.edges().size(), bp_.edges().size());
}

// Test invalid JSON deserialization
TEST_F(SerializationTest, InvalidJson) {
  // Try to deserialize invalid JSON
  auto deserialized = taskflow::workflow::serializer::from_json("invalid json");
  EXPECT_FALSE(deserialized.has_value());
}

// Test empty binary deserialization
TEST_F(SerializationTest, EmptyBinary) {
  // Try to deserialize empty binary data
  std::vector<std::uint8_t> empty_binary;
  auto deserialized = taskflow::workflow::serializer::from_binary(empty_binary);
  EXPECT_FALSE(deserialized.has_value());
}

// Test complex blueprint serialization
TEST_F(SerializationTest, ComplexBlueprint) {
  // Create a more complex blueprint
  taskflow::workflow::workflow_blueprint complex_bp;
  complex_bp.add_node(taskflow::workflow::node_def{1, "task1"});
  complex_bp.add_node(taskflow::workflow::node_def{2, "task2"});
  complex_bp.add_node(taskflow::workflow::node_def{3, "task3"});
  complex_bp.add_node(taskflow::workflow::node_def{4, "task4"});
  complex_bp.add_node(taskflow::workflow::node_def{5, "task5"});

  complex_bp.add_edge(taskflow::workflow::edge_def(1, 2));
  complex_bp.add_edge(taskflow::workflow::edge_def(1, 3));
  complex_bp.add_edge(taskflow::workflow::edge_def(2, 4));
  complex_bp.add_edge(taskflow::workflow::edge_def(3, 4));
  complex_bp.add_edge(taskflow::workflow::edge_def(3, 5));
  complex_bp.add_edge(taskflow::workflow::edge_def(4, 5));

  // Serialize and deserialize
  std::string json = taskflow::workflow::serializer::to_json(complex_bp);
  auto deserialized = taskflow::workflow::serializer::from_json(json);
  ASSERT_TRUE(deserialized.has_value());

  // Verify complex blueprint
  const auto& deserialized_bp = deserialized.value();
  EXPECT_EQ(deserialized_bp.nodes().size(), 5);
  EXPECT_EQ(deserialized_bp.edges().size(), 6);
}

TEST_F(SerializationTest, NodeLabelTagsRetryRoundTrip) {
  taskflow::workflow::node_def n{1, "worker"};
  n.label = "human_readable";
  n.tags = {"alpha", "beta"};
  taskflow::core::retry_policy rp;
  rp.max_attempts = 4;
  rp.initial_delay = std::chrono::milliseconds{15};
  rp.backoff_multiplier = 1.5f;
  rp.max_delay = std::chrono::milliseconds{200};
  rp.jitter = true;
  rp.jitter_range = std::chrono::milliseconds{7};
  n.retry = rp;

  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(n);
  auto json = taskflow::workflow::serializer::to_json(bp);
  auto out = taskflow::workflow::serializer::from_json(json);
  ASSERT_TRUE(out.has_value());
  const auto* p = out->find_node(1);
  ASSERT_NE(p, nullptr);
  ASSERT_TRUE(p->label.has_value());
  EXPECT_EQ(*p->label, "human_readable");
  ASSERT_EQ(p->tags.size(), 2u);
  ASSERT_TRUE(p->retry.has_value());
  EXPECT_EQ(p->retry->max_attempts, 4);
  EXPECT_EQ(p->retry->initial_delay.count(), 15);
  EXPECT_FLOAT_EQ(p->retry->backoff_multiplier, 1.5f);
  EXPECT_EQ(p->retry->max_delay.count(), 200);
  EXPECT_TRUE(p->retry->jitter);
  EXPECT_EQ(p->retry->jitter_range.count(), 7);
}

}  // namespace
