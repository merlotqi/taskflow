#pragma once

#include <taskflow/engine/execution.hpp>
#include <taskflow/workflow/blueprint.hpp>

#include <nlohmann/json.hpp>
#include <string>

namespace tf {

nlohmann::json blueprint_to_json(const WorkflowBlueprint& bp);
WorkflowBlueprint blueprint_from_json(const nlohmann::json& j);

std::string blueprint_to_string(const WorkflowBlueprint& bp);
WorkflowBlueprint blueprint_from_string(const std::string& s);

nlohmann::json execution_to_json(const WorkflowExecution& ex);
void execution_from_json(const nlohmann::json& j, WorkflowExecution& ex);

}  // namespace tf
