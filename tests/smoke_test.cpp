#include <gtest/gtest.h>

// Phase 0 smoke test: verifies the build/test toolchain (CMake + GoogleTest)
// is wired up correctly. Replaced by real suites in later phases.
TEST(Smoke, ToolchainWorks) {
  EXPECT_EQ(1 + 1, 2);
}
