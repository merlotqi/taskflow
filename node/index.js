'use strict';

const fs = require('node:fs');
const path = require('node:path');

function loadNativeBinding() {
  const candidates = [
    path.join(__dirname, 'build', 'Release', 'Taskflow.node'),
    path.join(__dirname, 'build', 'Debug', 'Taskflow.node'),
    path.join(__dirname, '..', 'build', 'node', 'Taskflow.node'),
    path.join(__dirname, '..', 'build', 'node', 'Release', 'Taskflow.node'),
    path.join(__dirname, '..', 'build', 'node', 'Debug', 'Taskflow.node')
  ];

  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      return require(candidate);
    }
  }

  throw new Error(
    'TaskFlow native addon was not found. Run `npm run build` inside node/ or build the top-level CMake target `taskflow_node`.'
  );
}

const native = loadNativeBinding();

function isPromiseLike(value) {
  return value && typeof value.then === 'function';
}

function blueprintKey(idOrName) {
  return typeof idOrName === 'string' ? `name:${idOrName}` : `id:${String(idOrName)}`;
}

function normalizeNode(input) {
  if (!input || typeof input !== 'object') {
    throw new TypeError('Blueprint node definitions must be objects');
  }

  const id = input.id;
  const taskType = input.taskType ?? input.task_type;
  if (typeof id === 'undefined' || typeof taskType !== 'string') {
    throw new TypeError('Blueprint node requires `id` and `taskType`/`task_type`');
  }

  return {
    id,
    taskType,
    label: input.label ?? null,
    retry: input.retry ?? null,
    compensateTaskType: input.compensateTaskType ?? input.compensate_task_type ?? null,
    compensateRetry: input.compensateRetry ?? input.compensate_retry ?? null,
    tags: Array.isArray(input.tags) ? input.tags : []
  };
}

function normalizeEdge(input) {
  if (!input || typeof input !== 'object') {
    throw new TypeError('Blueprint edge definitions must be objects');
  }

  if (typeof input.from === 'undefined' || typeof input.to === 'undefined') {
    throw new TypeError('Blueprint edge requires `from` and `to`');
  }

  return {
    from: input.from,
    to: input.to,
    condition: input.condition ?? input.when ?? null
  };
}

function metadataFromBlueprintInput(input) {
  if (input instanceof Blueprint) {
    return input.metadata();
  }

  const definition = typeof input === 'string' ? JSON.parse(input) : input;
  const nodes = Array.isArray(definition?.nodes) ? definition.nodes.map(normalizeNode) : [];
  const edges = Array.isArray(definition?.edges) ? definition.edges.map(normalizeEdge) : [];
  return {
    taskTypes: new Set(nodes.map((node) => node.taskType)),
    requiresAsyncExecution: edges.some((edge) => typeof edge.condition === 'function')
  };
}

function buildBlueprintFromDefinition(definition) {
  if (definition instanceof Blueprint) {
    return { blueprint: definition, temporary: false };
  }

  const blueprint = new Blueprint();
  blueprint.load(definition);
  return { blueprint, temporary: true };
}

function normalizeTaskCallback(fn) {
  if (typeof fn !== 'function') {
    throw new TypeError('Task callback must be a function');
  }

  return (ctx) => {
    const result = fn(ctx);
    if (isPromiseLike(result)) {
      throw new TypeError('Task callbacks must be synchronous. Use `runAsync` on the orchestrator/execution side.');
    }
    if (typeof result === 'undefined') {
      return native.TF_TASK_STATE_SUCCESS;
    }
    return result;
  };
}

class Blueprint {
  constructor(definition = null) {
    this.handle = native.createBlueprint();
    this.closed = false;
    this._taskTypes = new Set();
    this._requiresAsyncExecution = false;

    if (definition) {
      this.load(definition);
    }
  }

  ensureOpen() {
    if (this.closed) {
      throw new Error('Blueprint is already closed');
    }
  }

  load(definition) {
    this.ensureOpen();
    const data = typeof definition === 'string' ? JSON.parse(definition) : definition;
    const nodes = Array.isArray(data?.nodes) ? data.nodes : [];
    const edges = Array.isArray(data?.edges) ? data.edges : [];

    for (const node of nodes) {
      this.addNode(node);
    }
    for (const edge of edges) {
      this.addEdge(edge);
    }
    return this;
  }

  addNode(nodeOrId, taskType, options = {}) {
    this.ensureOpen();

    const normalized = typeof nodeOrId === 'object'
      ? normalizeNode(nodeOrId)
      : normalizeNode({ id: nodeOrId, taskType, ...options });

    native.blueprintAddNode(this.handle, normalized.id, normalized.taskType, normalized.label);
    this._taskTypes.add(normalized.taskType);

    if (normalized.retry) {
      native.blueprintSetNodeRetry(this.handle, normalized.id, normalized.retry);
    }
    if (normalized.compensateTaskType || normalized.compensateRetry) {
      native.blueprintSetNodeCompensation(
        this.handle,
        normalized.id,
        normalized.compensateTaskType,
        normalized.compensateRetry
      );
    }
    for (const tag of normalized.tags) {
      native.blueprintAddNodeTag(this.handle, normalized.id, tag);
    }
    return this;
  }

  addEdge(edgeOrFrom, toNodeId, maybeCondition = null) {
    this.ensureOpen();

    const normalized = typeof edgeOrFrom === 'object'
      ? normalizeEdge(edgeOrFrom)
      : normalizeEdge({ from: edgeOrFrom, to: toNodeId, condition: maybeCondition });

    if (typeof normalized.condition === 'function') {
      native.blueprintAddConditionalEdge(this.handle, normalized.from, normalized.to, normalized.condition);
      this._requiresAsyncExecution = true;
    } else {
      native.blueprintAddEdge(this.handle, normalized.from, normalized.to);
    }
    return this;
  }

  validate() {
    this.ensureOpen();
    return native.blueprintValidate(this.handle);
  }

  isValid() {
    this.ensureOpen();
    return native.blueprintIsValid(this.handle);
  }

  toJSON() {
    this.ensureOpen();
    return native.blueprintToJSON(this.handle);
  }

  toObject() {
    return JSON.parse(this.toJSON());
  }

  metadata() {
    return {
      taskTypes: new Set(this._taskTypes),
      requiresAsyncExecution: this._requiresAsyncExecution
    };
  }

  destroy() {
    if (this.closed) {
      return;
    }

    native.destroyBlueprint(this.handle);
    this.closed = true;
  }
}

class Execution {
  constructor(handle, options = {}) {
    this.handle = handle;
    this.closed = false;
    this.requiresAsyncExecution = options.requiresAsyncExecution === true;
    this.lastState = null;
  }

  ensureOpen() {
    if (this.closed) {
      throw new Error('Execution is already closed');
    }
  }

  id() {
    this.ensureOpen();
    return native.getExecutionId(this.handle);
  }

  snapshotJSON() {
    this.ensureOpen();
    return native.getExecutionSnapshot(this.handle);
  }

  snapshot() {
    return JSON.parse(this.snapshotJSON());
  }

  getNodeState(nodeId) {
    this.ensureOpen();
    return native.getNodeState(this.handle, nodeId);
  }

  getNodeStateName(nodeId) {
    return native.taskStateName(this.getNodeState(nodeId));
  }

  getOverallState() {
    this.ensureOpen();
    return native.getOverallState(this.handle);
  }

  getOverallStateName() {
    return native.taskStateName(this.getOverallState());
  }

  set(key, value) {
    this.ensureOpen();
    native.setExecutionContextValue(this.handle, key, value);
    return this;
  }

  get(key) {
    this.ensureOpen();
    return native.getExecutionContextValue(this.handle, key);
  }

  getResult(fromNodeId, key) {
    this.ensureOpen();
    return native.getExecutionResultValue(this.handle, fromNodeId, key);
  }

  runSync(options = {}) {
    this.ensureOpen();
    if (this.requiresAsyncExecution) {
      throw new Error('This execution uses JavaScript callbacks. Use orchestrator.runAsync(...) and await execution.wait().');
    }

    this.lastState = native.executionRunSync(this.handle, options);
    return this.lastState;
  }

  poll() {
    this.ensureOpen();
    const polled = native.pollExecution(this.handle);
    if (polled.ready) {
      this.lastState = polled.state;
    }
    return polled;
  }

  async wait() {
    this.ensureOpen();
    this.lastState = await native.waitExecutionAsync(this.handle);
    return this.lastState;
  }

  cancel() {
    this.ensureOpen();
    native.cancelExecution(this.handle);
  }

  isComplete() {
    this.ensureOpen();
    return native.isExecutionComplete(this.handle);
  }

  isCancelled() {
    this.ensureOpen();
    return native.isExecutionCancelled(this.handle);
  }

  destroy() {
    if (this.closed) {
      return;
    }

    native.destroyExecution(this.handle);
    this.closed = true;
  }
}

class Orchestrator {
  constructor(options = {}) {
    this.handle = native.createOrchestrator(options);
    this.closed = false;
    this._jsTaskTypes = new Set();
    this._blueprintMeta = new Map();
  }

  ensureOpen() {
    if (this.closed) {
      throw new Error('Orchestrator is already closed');
    }
  }

  registerTask(taskType, callback) {
    this.ensureOpen();
    if (typeof taskType !== 'string' || taskType.length === 0) {
      throw new TypeError('taskType must be a non-empty string');
    }

    native.registerTask(this.handle, taskType, normalizeTaskCallback(callback));
    this._jsTaskTypes.add(taskType);
    return this;
  }

  hasTask(taskType) {
    this.ensureOpen();
    return native.hasTask(this.handle, taskType);
  }

  registerBlueprint(idOrName, blueprintDefinition) {
    this.ensureOpen();

    const meta = metadataFromBlueprintInput(blueprintDefinition);
    const key = blueprintKey(idOrName);

    if (typeof idOrName === 'string' && typeof blueprintDefinition === 'string') {
      native.registerBlueprintNameJSON(this.handle, idOrName, blueprintDefinition);
    } else if (typeof idOrName !== 'string' && typeof blueprintDefinition === 'string') {
      native.registerBlueprintJSON(this.handle, idOrName, blueprintDefinition);
    } else {
      const { blueprint, temporary } = buildBlueprintFromDefinition(blueprintDefinition);
      try {
        if (typeof idOrName === 'string') {
          native.registerBlueprintName(this.handle, idOrName, blueprint.handle);
        } else {
          native.registerBlueprint(this.handle, idOrName, blueprint.handle);
        }
      } finally {
        if (temporary) {
          blueprint.destroy();
        }
      }
    }

    this._blueprintMeta.set(key, meta);
    return this;
  }

  hasBlueprint(idOrName) {
    this.ensureOpen();
    return typeof idOrName === 'string'
      ? native.hasBlueprintName(this.handle, idOrName)
      : native.hasBlueprint(this.handle, idOrName);
  }

  getBlueprintJSON(idOrName) {
    this.ensureOpen();
    return typeof idOrName === 'string'
      ? native.getBlueprintNameJSON(this.handle, idOrName)
      : native.getBlueprintJSON(this.handle, idOrName);
  }

  getBlueprint(idOrName) {
    return JSON.parse(this.getBlueprintJSON(idOrName));
  }

  requiresAsyncExecution(idOrName) {
    const meta = this._blueprintMeta.get(blueprintKey(idOrName));
    if (!meta) {
      return false;
    }
    if (meta.requiresAsyncExecution) {
      return true;
    }
    for (const taskType of meta.taskTypes) {
      if (this._jsTaskTypes.has(taskType)) {
        return true;
      }
    }
    return false;
  }

  createExecution(idOrName) {
    this.ensureOpen();
    const handle = typeof idOrName === 'string'
      ? native.createExecutionName(this.handle, idOrName)
      : native.createExecution(this.handle, idOrName);

    return new Execution(handle, { requiresAsyncExecution: this.requiresAsyncExecution(idOrName) });
  }

  runSync(idOrName, options = {}) {
    this.ensureOpen();
    if (this.requiresAsyncExecution(idOrName)) {
      throw new Error('This blueprint uses JavaScript callbacks. Use runAsync(...) and await execution.wait().');
    }

    const handle = typeof idOrName === 'string'
      ? native.runSyncName(this.handle, idOrName, options)
      : native.runSync(this.handle, idOrName, options);

    return new Execution(handle, { requiresAsyncExecution: false });
  }

  runAsync(idOrName, options = {}) {
    this.ensureOpen();
    const handle = typeof idOrName === 'string'
      ? native.runAsyncName(this.handle, idOrName, options)
      : native.runAsync(this.handle, idOrName, options);

    return new Execution(handle, { requiresAsyncExecution: this.requiresAsyncExecution(idOrName) });
  }

  cleanupCompletedExecutions() {
    this.ensureOpen();
    return native.cleanupCompletedExecutions(this.handle);
  }

  destroy() {
    if (this.closed) {
      return;
    }

    native.destroyOrchestrator(this.handle);
    this.closed = true;
  }
}

const constants = Object.fromEntries(
  Object.entries(native).filter(([key]) => key.startsWith('TF_'))
);

module.exports = {
  Blueprint,
  Orchestrator,
  Execution,
  TaskContext: native.TaskContext,
  native,
  constants
};
