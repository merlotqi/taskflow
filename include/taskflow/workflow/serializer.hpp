#pragma once

#include <optional>
#include <string>
#include <vector>

#include "taskflow/workflow/blueprint.hpp"

namespace taskflow::workflow {

class serializer {
 public:
  // Serialize to JSON string
  [[nodiscard]] static std::string to_json(const workflow_blueprint& bp);

  // Deserialize from JSON string
  [[nodiscard]] static std::optional<workflow_blueprint> from_json(const std::string& json);

  // Serialize to binary
  [[nodiscard]] static std::vector<std::uint8_t> to_binary(const workflow_blueprint& bp);

  // Deserialize from binary
  [[nodiscard]] static std::optional<workflow_blueprint> from_binary(const std::vector<std::uint8_t>& data);
};

}  // namespace taskflow::workflow
