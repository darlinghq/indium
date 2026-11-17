project(indium-test-texture-write)

add_executable(indium-test-texture-write texture-write.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/texture-write.h"
	COMMAND xxd -i -n "texture_write_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/texture-write.metallib" "${CMAKE_CURRENT_BINARY_DIR}/texture-write.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/texture-write.metallib"
)
target_sources(indium-test-texture-write PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/texture-write.h")

target_include_directories(indium-test-texture-write PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-texture-write PRIVATE indium indium_private)
set_target_properties(indium-test-texture-write PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
