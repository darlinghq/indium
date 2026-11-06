project(indium-test-encoder-interface)

add_executable(indium-test-encoder-interface encoder-interface.cpp)
target_link_libraries(indium-test-encoder-interface PRIVATE indium_kit indium_private)
set_target_properties(indium-test-encoder-interface PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
