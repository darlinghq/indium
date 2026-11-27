add_executable(indium-test-multisample-shader-write multisample-shader-write.cpp)
target_link_libraries(indium-test-multisample-shader-write PRIVATE indium indium_private)
set_target_properties(indium-test-multisample-shader-write PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
