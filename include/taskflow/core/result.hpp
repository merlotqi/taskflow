#pragma once

#include <optional>
#include <string>

namespace tf {

enum class ResultKind { Ok, Err };

struct ResultPayload {
  ResultKind kind{ResultKind::Ok};
  std::string message;
};

}  // namespace tf
