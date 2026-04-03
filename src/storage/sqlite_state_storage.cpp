#include "taskflow/storage/sqlite_state_storage.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace taskflow::storage {

sqlite_state_storage::sqlite_state_storage(std::string db_path) : path_(std::move(db_path)) {
  sqlite3* raw = nullptr;
  if (sqlite3_open(path_.c_str(), &raw) != SQLITE_OK) {
    std::string err = raw ? sqlite3_errmsg(raw) : "sqlite open failed";
    if (raw) sqlite3_close(raw);
    throw std::runtime_error(err);
  }
  db_ = raw;
  init_schema();
}

void sqlite_state_storage::close() {
  if (db_) {
    sqlite3_close(static_cast<sqlite3*>(db_));
    db_ = nullptr;
  }
}

sqlite_state_storage::~sqlite_state_storage() { close(); }

sqlite_state_storage::sqlite_state_storage(sqlite_state_storage&& o) noexcept : db_(o.db_), path_(std::move(o.path_)) {
  o.db_ = nullptr;
}

sqlite_state_storage& sqlite_state_storage::operator=(sqlite_state_storage&& o) noexcept {
  if (this != &o) {
    close();
    db_ = o.db_;
    path_ = std::move(o.path_);
    o.db_ = nullptr;
  }
  return *this;
}

void sqlite_state_storage::init_schema() {
  auto* db = static_cast<sqlite3*>(db_);
  const char* sql =
      "CREATE TABLE IF NOT EXISTS executions (id INTEGER PRIMARY KEY, blob TEXT NOT NULL);";
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "sqlite schema";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

void sqlite_state_storage::save(std::size_t exec_id, std::string blob) {
  auto* db = static_cast<sqlite3*>(db_);
  const char* sql = "INSERT OR REPLACE INTO executions (id, blob) VALUES (?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(exec_id));
  sqlite3_bind_text(stmt, 2, blob.c_str(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
}

std::optional<std::string> sqlite_state_storage::load(std::size_t exec_id) {
  auto* db = static_cast<sqlite3*>(db_);
  const char* sql = "SELECT blob FROM executions WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(exec_id));
  std::optional<std::string> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    int len = sqlite3_column_bytes(stmt, 0);
    if (text && len >= 0) {
      out = std::string(text, static_cast<std::size_t>(len));
    }
  }
  sqlite3_finalize(stmt);
  return out;
}

void sqlite_state_storage::remove(std::size_t exec_id) {
  auto* db = static_cast<sqlite3*>(db_);
  const char* sql = "DELETE FROM executions WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(exec_id));
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::vector<std::size_t> sqlite_state_storage::list_all() {
  auto* db = static_cast<sqlite3*>(db_);
  std::vector<std::size_t> ids;
  const char* sql = "SELECT id FROM executions;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ids.push_back(static_cast<std::size_t>(sqlite3_column_int64(stmt, 0)));
  }
  sqlite3_finalize(stmt);
  return ids;
}

}  // namespace taskflow::storage
