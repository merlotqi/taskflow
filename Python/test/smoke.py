from __future__ import annotations

import importlib.util
from pathlib import Path


def load_taskflow_module():
    root = Path(__file__).resolve().parents[2]
    candidates = list((root / "build" / "Python").glob("taskflow.cpython-*.so"))
    candidates += list((root / "build" / "Python").glob("taskflow*.pyd"))

    if not candidates:
        raise RuntimeError("taskflow Python extension not found; build the `taskflow_python` target first")

    module_path = candidates[0]
    spec = importlib.util.spec_from_file_location("taskflow", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load module spec from {module_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


taskflow = load_taskflow_module()


def main() -> None:
    orchestrator = taskflow.Orchestrator({"memory_result_storage": True})

    try:
        def fetch_order(ctx):
            assert ctx.exec_id() > 0
            assert ctx.node_id() == 1
            ctx.set("order_id", "order-42")
            ctx.set_result("payload", "order-42")
            return taskflow.constants.TF_TASK_STATE_SUCCESS

        def send_email(ctx):
            assert ctx.exec_id() > 0
            assert ctx.node_id() == 2
            assert ctx.get("order_id") == "order-42"
            assert ctx.get_result(1, "payload") == "order-42"
            ctx.set("email_status", "sent")
            return "success"

        orchestrator.register_task("fetch_order", fetch_order)
        orchestrator.register_task("send_email", send_email)

        blueprint = taskflow.Blueprint()
        blueprint.add_node({"id": 1, "task_type": "fetch_order", "label": "Fetch Order"})
        blueprint.add_node({"id": 2, "task_type": "send_email", "label": "Send Email"})
        blueprint.add_edge({"from": 1, "to": 2})

        orchestrator.register_blueprint(1, blueprint)
        blueprint.destroy()

        execution = orchestrator.run_sync(1)
        try:
            assert execution.get_overall_state() == taskflow.constants.TF_TASK_STATE_SUCCESS
            assert execution.get_node_state(1) == taskflow.constants.TF_TASK_STATE_SUCCESS
            assert execution.get_node_state(2) == taskflow.constants.TF_TASK_STATE_SUCCESS
            assert execution.get("order_id") == "order-42"
            assert execution.get("email_status") == "sent"
            assert execution.get_result(1, "payload") == "order-42"

            snapshot = execution.snapshot()
            print("=== TaskFlow Python Smoke Example ===")
            print("orchestrator handle:", orchestrator.handle)
            print("execution handle:", execution.handle)
            print("execution id:", execution.id())
            print("final state:", taskflow.task_state_name(execution.get_overall_state()))
            print("snapshot:", snapshot)
        finally:
            execution.destroy()
    finally:
        orchestrator.destroy()


if __name__ == "__main__":
    main()
