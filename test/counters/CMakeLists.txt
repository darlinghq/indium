project(indium-test-counters)

add_executable(indium-test-counters counters.cpp)
target_link_libraries(indium-test-counters PRIVATE indium indium_private)
set_target_properties(indium-test-counters PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
