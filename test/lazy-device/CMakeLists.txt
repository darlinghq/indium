project(indium-test-lazy-device)

add_executable(indium-test-lazy-device lazy-device.cpp)
target_link_libraries(indium-test-lazy-device PRIVATE indium_kit indium_private)
set_target_properties(indium-test-lazy-device PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
