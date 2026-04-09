#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "taskflow/core/state_storage.hpp"

namespace taskflow::storage {

/// Result of creating a `state_storage` backend, including effective backend after fallback.
struct state_storage_create_result {
  std::unique_ptr<core::state_storage> storage;
  /// Backend actually used (`memory` or `sqlite`).
  std::string resolved_backend;
  /// True when the requested backend was missing or the creator threw; `memory` was used instead.
  bool fell_back_to_memory = false;
};

/// Register and construct `core::state_storage` implementations by name (e.g. `memory`, `sqlite`).
class state_storage_factory {
 public:
  using creator_fn = std::function<std::unique_ptr<core::state_storage>(std::string_view config)>;

  /// Register or replace a backend. Names are compared case-insensitively.
  static void register_backend(std::string_view name, creator_fn creator);

  /// Ensures built-in backends (`memory`, and `sqlite` when compiled with SQLite) are registered.
  static void ensure_default_backends_registered();

  /// Create storage for `backend` with optional `config` (e.g. SQLite file path; empty uses `:memory:` for sqlite).
  /// On unknown backend or creator exception, returns `memory_state_storage` and sets `fell_back_to_memory`.
  [[nodiscard]] static state_storage_create_result create(std::string_view backend, std::string_view config = {});

  [[nodiscard]] static bool is_backend_registered(std::string_view name);

 private:
  static std::string normalize_name(std::string_view name);
};

}  // namespace taskflow::storage
