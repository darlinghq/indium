project(indium-test-buffer-no-copy)

add_executable(indium-test-buffer-no-copy buffer-no-copy.cpp)
target_link_libraries(indium-test-buffer-no-copy PRIVATE indium indium_private)
set_target_properties(indium-test-buffer-no-copy PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
