'use strict';

const assert = require('node:assert/strict');

const Taskflow = require('..');

async function main() {
  const orchestrator = new Taskflow.Orchestrator({ memoryResultStorage: true });

  try {
    orchestrator.registerTask('fetch_order', (ctx) => {
      ctx.set('orderId', 'order-42');
      ctx.setResult('payload', 'order-42');
      return Taskflow.constants.TF_TASK_STATE_SUCCESS;
    });

    orchestrator.registerTask('send_email', (ctx) => {
      const payload = ctx.getResult(1n, 'payload');
      assert.equal(payload, 'order-42');
      ctx.set('emailStatus', 'sent');
      return 'success';
    });

    const blueprintId = 1n;
    const blueprint = new Taskflow.Blueprint()
      .addNode({ id: 1, taskType: 'fetch_order', label: 'Fetch Order' })
      .addNode({ id: 2, taskType: 'send_email', label: 'Send Email' })
      .addEdge({ from: 1, to: 2 });

    console.log('=== TaskFlow Node.js Smoke Example ===');
    console.log('orchestrator handle:', orchestrator.handle.toString());
    console.log('register blueprint:', blueprintId.toString());
    console.log(blueprint.toJSON());

    orchestrator.registerBlueprint(blueprintId, blueprint);
    blueprint.destroy();

    const execution = orchestrator.runAsync(blueprintId);
    try {
      const finalState = await execution.wait();
      const snapshot = execution.snapshot();
      const nodeStateMap = new Map(snapshot.nodes.map((node) => [node.id, node]));

      assert.equal(finalState, Taskflow.constants.TF_TASK_STATE_SUCCESS);
      assert.equal(execution.isComplete(), true);
      assert.equal(execution.getOverallState(), Taskflow.constants.TF_TASK_STATE_SUCCESS);
      assert.equal(execution.getNodeState(1n), Taskflow.constants.TF_TASK_STATE_SUCCESS);
      assert.equal(execution.getNodeState(2n), Taskflow.constants.TF_TASK_STATE_SUCCESS);
      assert.equal(execution.get('orderId'), 'order-42');
      assert.equal(execution.get('emailStatus'), 'sent');
      assert.equal(execution.getResult(1n, 'payload'), 'order-42');
      assert.equal(nodeStateMap.get(1)?.state, 'success');
      assert.equal(nodeStateMap.get(2)?.state, 'success');

      console.log('execution handle:', execution.handle.toString());
      console.log('execution id:', execution.id().toString());
      console.log('final state:', Taskflow.native.taskStateName(finalState));
      console.log('snapshot:');
      console.log(JSON.stringify(snapshot, null, 2));
    } finally {
      execution.destroy();
      console.log('execution destroyed');
    }
  } finally {
    orchestrator.destroy();
    console.log('orchestrator destroyed');
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
