#include <gtest/gtest.h>

// Asserts nothing about the book: this exists so a failing CMake or GoogleTest
// wiring shows up as a build/test failure of its own.
TEST(Smoke, ToolchainWorks) {
  EXPECT_EQ(1 + 1, 2);
}
