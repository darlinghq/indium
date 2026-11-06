project(indium-test-encoder-barriers)

add_executable(indium-test-encoder-barriers encoder-barriers.cpp)
target_link_libraries(indium-test-encoder-barriers PRIVATE indium_kit indium_private)
set_target_properties(indium-test-encoder-barriers PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
