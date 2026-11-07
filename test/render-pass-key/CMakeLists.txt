project(indium-test-render-pass-key)

add_executable(indium-test-render-pass-key render-pass-key.cpp)
target_link_libraries(indium-test-render-pass-key PRIVATE indium_private)
set_target_properties(indium-test-render-pass-key PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_test(NAME render-pass-key COMMAND indium-test-render-pass-key)
