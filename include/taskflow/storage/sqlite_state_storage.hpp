#pragma once

#include <optional>
#include <string>
#include <vector>

#include "taskflow/core/state_storage.hpp"

namespace taskflow::storage {

/** Optional backend; build with -DTASKFLOW_WITH_SQLITE=ON and link SQLite3. */
class sqlite_state_storage final : public core::state_storage {
 public:
  explicit sqlite_state_storage(std::string db_path);
  ~sqlite_state_storage() override;

  sqlite_state_storage(const sqlite_state_storage&) = delete;
  sqlite_state_storage& operator=(const sqlite_state_storage&) = delete;
  sqlite_state_storage(sqlite_state_storage&&) noexcept;
  sqlite_state_storage& operator=(sqlite_state_storage&&) noexcept;

  void save(std::size_t exec_id, std::string blob) override;
  [[nodiscard]] std::optional<std::string> load(std::size_t exec_id) override;
  void remove(std::size_t exec_id) override;
  [[nodiscard]] std::vector<std::size_t> list_all() override;

 private:
  void* db_{nullptr};
  std::string path_;

  void close();
  void init_schema();
};

}  // namespace taskflow::storage
