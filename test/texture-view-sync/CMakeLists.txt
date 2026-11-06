project(indium-test-texture-view-sync)

add_executable(indium-test-texture-view-sync texture-view-sync.cpp)
target_link_libraries(indium-test-texture-view-sync PRIVATE indium_kit indium_private)
set_target_properties(indium-test-texture-view-sync PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
