#include <gtest/gtest.h>

#include <algorithm>

#include <taskflow/storage/sqlite_state_storage.hpp>

namespace {

// Test fixture for sqlite_state_storage
class SQLiteStateStorageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_ = std::make_unique<taskflow::storage::sqlite_state_storage>(":memory:");
  }

  void TearDown() override {
    storage_.reset();
  }

  std::unique_ptr<taskflow::storage::sqlite_state_storage> storage_;
};

// Test save and load functionality
TEST_F(SQLiteStateStorageTest, SaveLoad) {
  std::string data = "test_data_123";
  storage_->save(1, data);

  auto loaded = storage_->load(1);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded.value(), data);
}

// Test load non-existent data
TEST_F(SQLiteStateStorageTest, LoadNonExistent) {
  auto loaded = storage_->load(999);
  EXPECT_FALSE(loaded.has_value());
}

// Test remove functionality
TEST_F(SQLiteStateStorageTest, Remove) {
  storage_->save(2, "data_to_remove");
  storage_->remove(2);

  auto loaded = storage_->load(2);
  EXPECT_FALSE(loaded.has_value());
}

// Test list_all functionality
TEST_F(SQLiteStateStorageTest, ListAll) {
  storage_->save(3, "data1");
  storage_->save(4, "data2");
  storage_->save(5, "data3");

  auto all_ids = storage_->list_all();
  EXPECT_EQ(all_ids.size(), 3);
  EXPECT_TRUE(std::find(all_ids.begin(), all_ids.end(), 3) != all_ids.end());
  EXPECT_TRUE(std::find(all_ids.begin(), all_ids.end(), 4) != all_ids.end());
  EXPECT_TRUE(std::find(all_ids.begin(), all_ids.end(), 5) != all_ids.end());
}

// Test database persistence
TEST_F(SQLiteStateStorageTest, DatabasePersistence) {
  {
    taskflow::storage::sqlite_state_storage temp_storage(":memory:");
    temp_storage.save(6, "persistent_data");
  }

  // Recreate storage and verify data persistence
  taskflow::storage::sqlite_state_storage temp_storage(":memory:");
  auto loaded = temp_storage.load(6);
  EXPECT_FALSE(loaded.has_value()); // In-memory DB should not persist
}

// Test special characters in data
TEST_F(SQLiteStateStorageTest, SpecialCharacters) {
  std::string special_data = "test\nwith\tspecial\tcharacters\r\n";
  storage_->save(7, special_data);

  auto loaded = storage_->load(7);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded.value(), special_data);
}

// Test large data
TEST_F(SQLiteStateStorageTest, LargeData) {
  std::string large_data(10000, 'a'); // 10KB of 'a' characters
  storage_->save(8, large_data);

  auto loaded = storage_->load(8);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded.value(), large_data);
}

// Test multiple operations
TEST_F(SQLiteStateStorageTest, MultipleOperations) {
  storage_->save(9, "data1");
  storage_->save(10, "data2");
  storage_->remove(9);

  auto loaded1 = storage_->load(9);
  auto loaded2 = storage_->load(10);

  EXPECT_FALSE(loaded1.has_value());
  ASSERT_TRUE(loaded2.has_value());
  EXPECT_EQ(loaded2.value(), "data2");
}

// Test move semantics
TEST_F(SQLiteStateStorageTest, MoveSemantics) {
  storage_->save(11, "movable_data");

  taskflow::storage::sqlite_state_storage moved_storage = std::move(*storage_);
  auto loaded = moved_storage.load(11);

  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded.value(), "movable_data");
}

}  // namespace
