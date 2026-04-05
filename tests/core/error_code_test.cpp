#include <gtest/gtest.h>

#include <taskflow/core/error_code.hpp>

namespace {

using taskflow::core::errc;
using taskflow::core::make_error_code;

TEST(ErrorCodeTest, CategoryName) {
  const std::error_category& cat = taskflow::core::taskflow_category();
  EXPECT_STREQ(cat.name(), "taskflow");
}

TEST(ErrorCodeTest, SuccessMessage) {
  auto ec = make_error_code(errc::success);
  EXPECT_EQ(ec.category().name(), std::string("taskflow"));
  EXPECT_EQ(ec.message(), "success");
}

TEST(ErrorCodeTest, RepresentativeMessages) {
  EXPECT_EQ(make_error_code(errc::blueprint_not_found).message(), "blueprint not found");
  EXPECT_EQ(make_error_code(errc::execution_not_found).message(), "execution not found");
  EXPECT_EQ(make_error_code(errc::invalid_state_transition).message(), "invalid state transition");
  EXPECT_EQ(make_error_code(errc::max_retry_exceeded).message(), "maximum retry attempts exceeded");
}

TEST(ErrorCodeTest, UnknownCodeFallsBack) {
  std::error_code ec{999, taskflow::core::taskflow_category()};
  EXPECT_EQ(ec.message(), "unknown error");
}

TEST(ErrorCodeTest, AllEnumeratedErrcHaveNonUnknownMessages) {
  const int max_ev = 13;
  for (int ev = 0; ev <= max_ev; ++ev) {
    std::error_code code{ev, taskflow::core::taskflow_category()};
    EXPECT_FALSE(code.message().empty());
    EXPECT_NE(code.message(), "unknown error") << "ev=" << ev;
  }
}

}  // namespace
