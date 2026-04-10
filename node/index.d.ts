declare namespace Taskflow {
  type BigIntLike = bigint | number;
  type TaskValue = boolean | number | bigint | string;
  type TaskValueOrUndefined = TaskValue | undefined;

  type TaskState =
    | 0
    | 1
    | 2
    | 3
    | 4
    | 5
    | 6
    | 7
    | 8
    | 9;

  type TaskCallbackStateName = 'success' | 'failed' | 'cancelled' | 'skipped';
  type TaskCallbackResult = TaskState | TaskCallbackStateName | boolean | null | undefined;

  interface RunOptions {
    stopOnFirstFailure?: boolean;
    compensateOnFailure?: boolean;
    compensateOnCancel?: boolean;
  }

  interface OrchestratorOptions {
    memoryStateStorage?: boolean;
    memoryResultStorage?: boolean;
  }

  interface RetryPolicy {
    maxAttempts?: number;
    initialDelayMs?: BigIntLike;
    backoffMultiplier?: number;
    maxDelayMs?: BigIntLike;
    jitter?: boolean;
    jitterRangeMs?: BigIntLike;
  }

  interface BlueprintNodeOptions {
    label?: string | null;
    retry?: RetryPolicy | null;
    compensateTaskType?: string | null;
    compensateRetry?: RetryPolicy | null;
    tags?: string[];
  }

  interface BlueprintNodeDefinition extends BlueprintNodeOptions {
    id: BigIntLike;
    taskType?: string;
    task_type?: string;
    compensate_task_type?: string | null;
    compensate_retry?: RetryPolicy | null;
  }

  type EdgeCondition = (ctx: TaskContext) => boolean;

  interface BlueprintEdgeDefinition {
    from: BigIntLike;
    to: BigIntLike;
    condition?: EdgeCondition | null;
    when?: EdgeCondition | null;
  }

  interface BlueprintDefinition {
    nodes?: BlueprintNodeDefinition[];
    edges?: BlueprintEdgeDefinition[];
    [key: string]: unknown;
  }

  interface ReadonlyStringSetLike {
    readonly size: number;
    has(value: string): boolean;
    forEach(callbackfn: (value: string, value2: string) => void): void;
  }

  interface BlueprintMetadata {
    taskTypes: ReadonlyStringSetLike;
    requiresAsyncExecution: boolean;
  }

  interface ExecutionPollResult {
    ready: boolean;
    state: TaskState;
    stateName: string;
  }

  interface ExecutionNodeSnapshot {
    id: number;
    state: string;
    retry_count?: number;
    error?: string;
    [key: string]: unknown;
  }

  interface ExecutionSnapshot {
    nodes: ExecutionNodeSnapshot[];
    [key: string]: unknown;
  }

  class TaskContext {
    private constructor();

    execId(): bigint;
    nodeId(): bigint;
    contains(key: string): boolean;
    get(key: string): TaskValueOrUndefined;
    set(key: string, value: TaskValue): void;
    getResult(fromNodeId: BigIntLike, key: string): TaskValueOrUndefined;
    setResult(key: string, value: TaskValue): void;
    progress(): number;
    reportProgress(progress: number): void;
    isCancelled(): boolean;
    cancel(): void;
    setError(message: string): void;
  }

  type TaskCallback = (ctx: TaskContext) => TaskCallbackResult;

  class Blueprint {
    constructor(definition?: BlueprintDefinition | string | null);

    readonly handle: bigint;
    readonly closed: boolean;

    load(definition: BlueprintDefinition | string): this;
    addNode(node: BlueprintNodeDefinition): this;
    addNode(nodeId: BigIntLike, taskType: string, options?: BlueprintNodeOptions): this;
    addEdge(edge: BlueprintEdgeDefinition): this;
    addEdge(fromNodeId: BigIntLike, toNodeId: BigIntLike, condition?: EdgeCondition | null): this;
    validate(): string;
    isValid(): boolean;
    toJSON(): string;
    toObject(): BlueprintDefinition;
    metadata(): BlueprintMetadata;
    destroy(): void;
  }

  interface ExecutionOptions {
    requiresAsyncExecution?: boolean;
  }

  class Execution {
    constructor(handle: bigint, options?: ExecutionOptions);

    readonly handle: bigint;
    readonly closed: boolean;
    readonly requiresAsyncExecution: boolean;
    readonly lastState: TaskState | null;

    id(): bigint;
    snapshotJSON(): string;
    snapshot(): ExecutionSnapshot;
    getNodeState(nodeId: BigIntLike): TaskState;
    getNodeStateName(nodeId: BigIntLike): string;
    getOverallState(): TaskState;
    getOverallStateName(): string;
    set(key: string, value: TaskValue): this;
    get(key: string): TaskValueOrUndefined;
    getResult(fromNodeId: BigIntLike, key: string): TaskValueOrUndefined;
    runSync(options?: RunOptions): TaskState;
    poll(): ExecutionPollResult;
    wait(): Promise<TaskState>;
    cancel(): void;
    isComplete(): boolean;
    isCancelled(): boolean;
    destroy(): void;
  }

  interface NativeBinding extends Constants {
    readonly TaskContext: typeof TaskContext;
    errorMessage(code: number): string;
    taskStateName(state: number): string;
    [name: string]: unknown;
  }

  interface Constants {
    readonly TF_OK: 0;
    readonly TF_ERROR_UNKNOWN: 1;
    readonly TF_ERROR_INVALID_HANDLE: 2;
    readonly TF_ERROR_BLUEPRINT_NOT_FOUND: 3;
    readonly TF_ERROR_EXECUTION_NOT_FOUND: 4;
    readonly TF_ERROR_BUS_SHUTDOWN: 5;
    readonly TF_ERROR_INVALID_STATE: 6;
    readonly TF_ERROR_INVALID_ARGUMENT: 7;
    readonly TF_ERROR_TASK_TYPE_NOT_FOUND: 8;
    readonly TF_ERROR_OPERATION_NOT_PERMITTED: 9;
    readonly TF_ERROR_STORAGE: 10;
    readonly TF_ERROR_TYPE_MISMATCH: 11;
    readonly TF_ERROR_VALUE_NOT_FOUND: 12;
    readonly TF_ERROR_NOT_SUPPORTED: 13;
    readonly TF_TASK_STATE_PENDING: 0;
    readonly TF_TASK_STATE_RUNNING: 1;
    readonly TF_TASK_STATE_SUCCESS: 2;
    readonly TF_TASK_STATE_FAILED: 3;
    readonly TF_TASK_STATE_RETRY: 4;
    readonly TF_TASK_STATE_SKIPPED: 5;
    readonly TF_TASK_STATE_CANCELLED: 6;
    readonly TF_TASK_STATE_COMPENSATING: 7;
    readonly TF_TASK_STATE_COMPENSATED: 8;
    readonly TF_TASK_STATE_COMPENSATION_FAILED: 9;
  }

  class Orchestrator {
    constructor(options?: OrchestratorOptions);

    readonly handle: bigint;
    readonly closed: boolean;

    registerTask(taskType: string, callback: TaskCallback): this;
    hasTask(taskType: string): boolean;
    registerBlueprint(idOrName: BigIntLike | string, blueprintDefinition: Blueprint | BlueprintDefinition | string): this;
    hasBlueprint(idOrName: BigIntLike | string): boolean;
    getBlueprintJSON(idOrName: BigIntLike | string): string;
    getBlueprint(idOrName: BigIntLike | string): BlueprintDefinition;
    requiresAsyncExecution(idOrName: BigIntLike | string): boolean;
    createExecution(idOrName: BigIntLike | string): Execution;
    runSync(idOrName: BigIntLike | string, options?: RunOptions): Execution;
    runAsync(idOrName: BigIntLike | string, options?: RunOptions): Execution;
    cleanupCompletedExecutions(): number;
    destroy(): void;
  }
}
