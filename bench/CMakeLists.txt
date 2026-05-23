# Benchmarks build to standalone executables (no test framework dependency).
add_executable(smoke_bench smoke_bench.cpp)
target_link_libraries(smoke_bench PRIVATE lob)
