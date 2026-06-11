# ITCH 5.0 demo tools: a synthetic feed generator and a book-reconstruction
# replayer that reports throughput. Real NASDAQ sample feeds are multi-gigabyte;
# the generator produces a spec-faithful stream so the pipeline is runnable.
add_executable(itch_gen itch_gen.cpp)
target_link_libraries(itch_gen PRIVATE lob)

add_executable(itch_replay itch_replay.cpp)
target_link_libraries(itch_replay PRIVATE lob)
