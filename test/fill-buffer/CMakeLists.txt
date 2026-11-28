project(indium-test-fill-buffer)

add_executable(indium-test-fill-buffer fill-buffer.cpp)
target_link_libraries(indium-test-fill-buffer PRIVATE indium indium_private)
set_target_properties(indium-test-fill-buffer PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
