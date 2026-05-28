include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
# Avoid GoogleTest overriding parent project compiler/linker settings on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Helper to register a GoogleTest executable + CTest entry.
function(lob_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE lob GTest::gtest_main)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

lob_add_test(smoke_test smoke_test.cpp)
lob_add_test(matching_test matching_test.cpp)
lob_add_test(structures_test structures_test.cpp)
