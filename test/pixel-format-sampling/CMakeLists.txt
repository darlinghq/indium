project(indium-test-pixel-format-sampling)

add_executable(indium-test-pixel-format-sampling pixel-format-sampling.cpp)
target_link_libraries(indium-test-pixel-format-sampling PRIVATE indium_kit indium_private)
set_target_properties(indium-test-pixel-format-sampling PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
