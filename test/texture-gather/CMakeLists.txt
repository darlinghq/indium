project(indium-test-texture-gather)

add_executable(indium-test-texture-gather texture-gather.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/texture-gather.h"
	COMMAND xxd -i -n "texture_gather_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/texture-gather.metallib" "${CMAKE_CURRENT_BINARY_DIR}/texture-gather.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/texture-gather.metallib"
)
target_sources(indium-test-texture-gather PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/texture-gather.h")

target_include_directories(indium-test-texture-gather PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-texture-gather PRIVATE indium indium_private)
set_target_properties(indium-test-texture-gather PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
