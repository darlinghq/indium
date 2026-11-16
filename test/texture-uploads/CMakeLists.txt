project(indium-test-texture-uploads)

add_executable(indium-test-texture-uploads texture-uploads.cpp)
target_link_libraries(indium-test-texture-uploads PRIVATE indium_kit indium_private)
set_target_properties(indium-test-texture-uploads PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
