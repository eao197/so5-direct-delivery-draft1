add_executable(agent_ring main.cpp)
target_link_libraries(agent_ring mimalloc-static sobjectizer::StaticLib)

