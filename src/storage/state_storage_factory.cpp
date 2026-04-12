#include "taskflow/storage/state_storage_factory.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <mutex>
#include <unordered_map>

#include "taskflow/storage/memory_state_storage.hpp"
#if defined(TASKFLOW_HAS_SQLITE)
#include "taskflow/storage/sqlite_state_storage.hpp"
#endif

namespace taskflow::storage {

namespace {

std::mutex& registry_mutex() {
  static std::mutex m;
  return m;
}

std::unordered_map<std::string, state_storage_factory::creator_fn>& registry() {
  static std::unordered_map<std::string, state_storage_factory::creator_fn> r;
  return r;
}

std::once_flag& defaults_once() {
  static std::once_flag f;
  return f;
}

void register_defaults_impl() {
  state_storage_factory::register_backend(
      "memory", [](std::string_view /*config*/) { return std::make_unique<memory_state_storage>(); });
#if defined(TASKFLOW_HAS_SQLITE)
  state_storage_factory::register_backend("sqlite", [](std::string_view config) {
    std::string path = config.empty() ? std::string(":memory:") : std::string(config);
    return std::unique_ptr<core::state_storage>(new sqlite_state_storage(std::move(path)));
  });
#endif
}

}  // namespace

std::string state_storage_factory::normalize_name(std::string_view name) {
  std::string out(name);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

void state_storage_factory::register_backend(std::string_view name, creator_fn creator) {
  std::lock_guard<std::mutex> lock(registry_mutex());
  registry()[normalize_name(name)] = std::move(creator);
}

void state_storage_factory::ensure_default_backends_registered() {
  std::call_once(defaults_once(), register_defaults_impl);
}

bool state_storage_factory::is_backend_registered(std::string_view name) {
  ensure_default_backends_registered();
  std::lock_guard<std::mutex> lock(registry_mutex());
  return registry().find(normalize_name(name)) != registry().end();
}

state_storage_create_result state_storage_factory::create(std::string_view backend, std::string_view config) {
  ensure_default_backends_registered();

  const std::string key = normalize_name(backend);
  creator_fn fn;
  {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto it = registry().find(key);
    if (it != registry().end()) {
      fn = it->second;
    }
  }

  state_storage_create_result out;
  if (!fn) {
    out.storage = std::make_unique<memory_state_storage>();
    out.resolved_backend = "memory";
    out.fell_back_to_memory = true;
    return out;
  }

  try {
    out.storage = fn(config);
    if (!out.storage) {
      out.storage = std::make_unique<memory_state_storage>();
      out.resolved_backend = "memory";
      out.fell_back_to_memory = true;
      return out;
    }
    out.resolved_backend = key;
    out.fell_back_to_memory = false;
    return out;
  } catch (const std::exception&) {
    out.storage = std::make_unique<memory_state_storage>();
    out.resolved_backend = "memory";
    out.fell_back_to_memory = true;
    return out;
  }
}

}  // namespace taskflow::storage
