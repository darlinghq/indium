project(indium-test-device-info)

add_executable(indium-test-device-info device-info.cpp)
target_link_libraries(indium-test-device-info PRIVATE indium_kit indium_private)
set_target_properties(indium-test-device-info PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
