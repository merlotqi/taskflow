#include <gtest/gtest.h>

#include <taskflow/storage/memory_state_storage.hpp>

namespace {

// Test fixture for memory_state_storage
class MemoryStateStorageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_ = std::make_unique<taskflow::storage::memory_state_storage>();
  }

  std::unique_ptr<taskflow::storage::memory_state_storage> storage_;
};

// Test save and load functionality
TEST_F(MemoryStateStorageTest, SaveLoad) {
  std::string data = "test_data_123";
  storage_->save(1, data);

  auto loaded = storage_->load(1);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded.value(), data);
}

// Test load non-existent data
TEST_F(MemoryStateStorageTest, LoadNonExistent) {
  auto loaded = storage_->load(999);
  EXPECT_FALSE(loaded.has_value());
}

// Test remove functionality
TEST_F(MemoryStateStorageTest, Remove) {
  storage_->save(2, "data_to_remove");
  storage_->remove(2);

  auto loaded = storage_->load(2);
  EXPECT_FALSE(loaded.has_value());
}

// Test list_all functionality
TEST_F(MemoryStateStorageTest, ListAll) {
  storage_->save(3, "data1");
  storage_->save(4, "data2");
  storage_->save(5, "data3");

  auto all_ids = storage_->list_all();
  EXPECT_EQ(all_ids.size(), 3);
  EXPECT_TRUE(std::find(all_ids.begin(), all_ids.end(), 3) != all_ids.end());
  EXPECT_TRUE(std::find(all_ids.begin(), all_ids.end(), 4) != all_ids.end());
  EXPECT_TRUE(std::find(all_ids.begin(), all_ids.end(), 5) != all_ids.end());
}

// Test size functionality
TEST_F(MemoryStateStorageTest, Size) {
  EXPECT_EQ(storage_->size(), 0);

  storage_->save(6, "data1");
  EXPECT_EQ(storage_->size(), 1);

  storage_->save(7, "data2");
  EXPECT_EQ(storage_->size(), 2);

  storage_->remove(6);
  EXPECT_EQ(storage_->size(), 1);
}

// Test clear functionality
TEST_F(MemoryStateStorageTest, Clear) {
  storage_->save(8, "data1");
  storage_->save(9, "data2");

  storage_->clear();

  EXPECT_EQ(storage_->size(), 0);
  EXPECT_FALSE(storage_->load(8).has_value());
  EXPECT_FALSE(storage_->load(9).has_value());
}

// Test concurrent access
TEST_F(MemoryStateStorageTest, ConcurrentAccess) {
  // This test is more of a sanity check since we can't easily test true concurrency
  // in a single-threaded test environment
  storage_->save(10, "data1");
  storage_->save(11, "data2");

  auto loaded1 = storage_->load(10);
  auto loaded2 = storage_->load(11);

  ASSERT_TRUE(loaded1.has_value());
  ASSERT_TRUE(loaded2.has_value());

  EXPECT_EQ(loaded1.value(), "data1");
  EXPECT_EQ(loaded2.value(), "data2");
}

// Test empty storage
TEST_F(MemoryStateStorageTest, EmptyStorage) {
  EXPECT_EQ(storage_->size(), 0);
  EXPECT_FALSE(storage_->load(100).has_value());
  EXPECT_TRUE(storage_->list_all().empty());
}

// Test large data
TEST_F(MemoryStateStorageTest, LargeData) {
  std::string large_data(10000, 'a'); // 10KB of 'a' characters
  storage_->save(12, large_data);

  auto loaded = storage_->load(12);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded.value(), large_data);
}

// Test special characters in data
TEST_F(MemoryStateStorageTest, SpecialCharacters) {
  std::string special_data = "test\nwith\tspecial\tcharacters\r\n";
  storage_->save(13, special_data);

  auto loaded = storage_->load(13);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded.value(), special_data);
}

}  // namespace
