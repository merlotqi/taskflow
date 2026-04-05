#include <gtest/gtest.h>

#include <any>
#include <string>

#include <taskflow/storage/memory_result_storage.hpp>

namespace {

using taskflow::core::result_locator;
using taskflow::storage::memory_result_storage;

TEST(MemoryResultStorageTest, StoreLoadRoundTrip) {
  memory_result_storage st;
  auto loc = st.store(1, 2, "k", std::any(7));
  EXPECT_EQ(loc.node_id, 2u);
  EXPECT_EQ(loc.key, "k");
  auto v = st.load(loc);
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(std::any_cast<int>(*v), 7);
}

TEST(MemoryResultStorageTest, ExistsRemove) {
  memory_result_storage st;
  result_locator loc{1, "x"};
  st.store(0, 1, "x", std::any(std::string("hi")));
  EXPECT_TRUE(st.exists(loc));
  EXPECT_TRUE(st.remove(loc));
  EXPECT_FALSE(st.exists(loc));
  EXPECT_FALSE(st.load(loc).has_value());
}

TEST(MemoryResultStorageTest, ListReturnsLocators) {
  memory_result_storage st;
  (void)st.store(9, 1, "a", std::any(1));
  (void)st.store(9, 2, "b", std::any(2));
  auto lst = st.list(9);
  EXPECT_EQ(lst.size(), 2u);
}

TEST(MemoryResultStorageTest, ClearExecClearsAll) {
  memory_result_storage st;
  st.store(1, 1, "a", std::any(1));
  st.store(2, 2, "b", std::any(2));
  st.clear(999);
  EXPECT_TRUE(st.list(1).empty());
}

TEST(MemoryResultStorageTest, ClearAll) {
  memory_result_storage st;
  st.store(1, 1, "a", std::any(1));
  st.clear_all();
  EXPECT_TRUE(st.list(1).empty());
}

}  // namespace
