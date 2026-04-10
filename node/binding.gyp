{
  "targets": [
    {
      "target_name": "taskflow",

      "sources": [
        "../src/core/audit_log.cpp",
        "../src/core/task_ctx.cpp",
        "../src/engine/default_thread_pool.cpp",
        "../src/engine/execution.cpp",
        "../src/engine/execution_snapshot.cpp",
        "../src/engine/executor.cpp",
        "../src/engine/orchestrator.cpp",
        "../src/engine/registry.cpp",
        "../src/engine/scheduler.cpp",
        "../src/obs/logger.cpp",
        "../src/obs/metrics.cpp",
        "../src/storage/memory_result_storage.cpp",
        "../src/storage/memory_state_storage.cpp",
        "../src/storage/state_storage_factory.cpp",
        "../src/workflow/blueprint.cpp",
        "../src/workflow/serializer.cpp",
        "../capi/taskflow_c.cpp",
        "src/node_binding.cpp"
      ],

      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../include"
      ],

      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],

      "cflags_cc": [
        "-std=c++17",
        "-fexceptions",
        "-pthread"
      ],

      "cflags!": [
        "-fno-exceptions"
      ],

      "cflags_cc!": [
        "-fno-exceptions"
      ],

      "libraries": [
        "-pthread"
      ],

      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS"
      ],

      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1
            }
          }
        }],
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
          }
        }]
      ]
    }
  ]
}
