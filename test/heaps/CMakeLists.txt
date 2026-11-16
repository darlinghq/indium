project(indium-test-heaps)

add_executable(indium-test-heaps heaps.cpp)
target_link_libraries(indium-test-heaps PRIVATE indium_kit indium_private)
set_target_properties(indium-test-heaps PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
