add_executable(itch_gen itch_gen.cpp)
target_link_libraries(itch_gen PRIVATE lob)

add_executable(itch_replay itch_replay.cpp)
target_link_libraries(itch_replay PRIVATE lob)
