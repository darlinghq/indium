add_executable(indium-test-texture-usage texture-usage.cpp)
target_link_libraries(indium-test-texture-usage PRIVATE indium indium_private)
set_target_properties(indium-test-texture-usage PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
