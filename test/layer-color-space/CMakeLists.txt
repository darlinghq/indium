project(indium-test-layer-color-space)

find_package(X11 REQUIRED)

add_executable(indium-test-layer-color-space layer-color-space.cpp)
target_include_directories(indium-test-layer-color-space PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(indium-test-layer-color-space PRIVATE indium_kit indium_private ${X11_LIBRARIES})
set_target_properties(indium-test-layer-color-space PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
