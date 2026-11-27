add_executable(indium-test-synchronize-resource synchronize-resource.cpp)
target_link_libraries(indium-test-synchronize-resource PRIVATE indium indium_private)
set_target_properties(indium-test-synchronize-resource PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
