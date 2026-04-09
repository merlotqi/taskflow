#include <gtest/gtest.h>

#include <taskflow/storage/state_storage_factory.hpp>

TEST(StateStorageFactoryTest, MemoryBackend) {
  auto r = taskflow::storage::state_storage_factory::create("memory", "");
  ASSERT_TRUE(r.storage);
  EXPECT_EQ(r.resolved_backend, "memory");
  EXPECT_FALSE(r.fell_back_to_memory);

  r.storage->save(7, "blob");
  auto loaded = r.storage->load(7);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(*loaded, "blob");
}

TEST(StateStorageFactoryTest, UnknownBackendFallsBackToMemory) {
  auto r = taskflow::storage::state_storage_factory::create("no_such_backend_xyz", "");
  ASSERT_TRUE(r.storage);
  EXPECT_EQ(r.resolved_backend, "memory");
  EXPECT_TRUE(r.fell_back_to_memory);
}

TEST(StateStorageFactoryTest, CaseInsensitiveName) {
  auto r = taskflow::storage::state_storage_factory::create("MEMORY", "");
  ASSERT_TRUE(r.storage);
  EXPECT_EQ(r.resolved_backend, "memory");
  EXPECT_FALSE(r.fell_back_to_memory);
}

#if defined(TASKFLOW_HAS_SQLITE)
TEST(StateStorageFactoryTest, SqliteInMemoryBackend) {
  auto r = taskflow::storage::state_storage_factory::create("sqlite", ":memory:");
  ASSERT_TRUE(r.storage);
  EXPECT_EQ(r.resolved_backend, "sqlite");
  EXPECT_FALSE(r.fell_back_to_memory);

  r.storage->save(1, "x");
  EXPECT_EQ(r.storage->load(1).value_or(""), "x");
}
#endif

TEST(StateStorageFactoryTest, CustomRegisterBackend) {
  taskflow::storage::state_storage_factory::register_backend(
      "test_custom", [](std::string_view c) -> std::unique_ptr<taskflow::core::state_storage> {
        if (c == "fail") {
          throw std::runtime_error("boom");
        }
        return taskflow::storage::state_storage_factory::create("memory", "").storage;
      });

  auto ok = taskflow::storage::state_storage_factory::create("test_custom", "ok");
  EXPECT_FALSE(ok.fell_back_to_memory);
  EXPECT_EQ(ok.resolved_backend, "test_custom");

  auto bad = taskflow::storage::state_storage_factory::create("test_custom", "fail");
  EXPECT_TRUE(bad.fell_back_to_memory);
  EXPECT_EQ(bad.resolved_backend, "memory");
}
