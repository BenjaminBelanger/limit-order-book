# Benchmarks build to standalone executables (no test framework dependency).
find_package(Threads REQUIRED)

add_executable(bench_main bench_main.cpp)
target_link_libraries(bench_main PRIVATE lob Threads::Threads)
target_include_directories(bench_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
