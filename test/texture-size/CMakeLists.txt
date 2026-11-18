project(indium-test-texture-size)

add_executable(indium-test-texture-size texture-size.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/texture-size.h"
	COMMAND xxd -i -n "texture_size_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/texture-size.metallib" "${CMAKE_CURRENT_BINARY_DIR}/texture-size.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/texture-size.metallib"
)
target_sources(indium-test-texture-size PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/texture-size.h")

target_include_directories(indium-test-texture-size PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-texture-size PRIVATE indium indium_private)
set_target_properties(indium-test-texture-size PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
