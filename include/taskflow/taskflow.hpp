#pragma once

// Core
#include <taskflow/core/audit_log.hpp>
#include <taskflow/core/cpo.hpp>
#include <taskflow/core/error_code.hpp>
#include <taskflow/core/result.hpp>
#include <taskflow/core/result_storage.hpp>
#include <taskflow/core/state_storage.hpp>
#include <taskflow/core/task.hpp>
#include <taskflow/core/task_concepts.hpp>
#include <taskflow/core/task_ctx.hpp>

// Workflow
#include <taskflow/workflow/blueprint.hpp>
#include <taskflow/workflow/edge.hpp>
#include <taskflow/workflow/node.hpp>
#include <taskflow/workflow/serializer.hpp>

// Engine
#include <taskflow/engine/default_thread_pool.hpp>
#include <taskflow/engine/execution.hpp>
#include <taskflow/engine/executor.hpp>
#include <taskflow/engine/orchestrator.hpp>
#include <taskflow/engine/parallel_executor.hpp>
#include <taskflow/engine/registry.hpp>
#include <taskflow/engine/scheduler.hpp>
#include <taskflow/engine/sync_executor.hpp>

// Storage
#include <taskflow/storage/memory_result_storage.hpp>
#include <taskflow/storage/memory_state_storage.hpp>
#include <taskflow/storage/state_storage_factory.hpp>
#if defined(TASKFLOW_HAS_SQLITE)
#include <taskflow/storage/sqlite_state_storage.hpp>
#endif

// Integration (MQ-style hooks, FFI-friendly events)
#include <taskflow/integration/event_hooks.hpp>

// Observer
#include <taskflow/obs/hooks_observer.hpp>
#include <taskflow/obs/logger.hpp>
#include <taskflow/obs/metrics.hpp>
#include <taskflow/obs/observer.hpp>

namespace taskflow {

using namespace core;
using namespace engine;
using namespace storage;
using namespace obs;
using namespace workflow;

}  // namespace taskflow
