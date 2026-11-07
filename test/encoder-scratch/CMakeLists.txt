project(indium-test-encoder-scratch)

add_executable(indium-test-encoder-scratch encoder-scratch.cpp)

# the control_flow kernel of air-ops
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h"
	COMMAND xxd -i -n "air_ops_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/../air-ops/air-ops.metallib" "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../air-ops/air-ops.metallib"
)
# the vertex and fragment shaders of triangle
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/AAPLShaders.h"
	COMMAND xxd -i -n "shader" "${CMAKE_CURRENT_SOURCE_DIR}/../triangle/AAPLShaders.metallib" "${CMAKE_CURRENT_BINARY_DIR}/AAPLShaders.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../triangle/AAPLShaders.metallib"
)
target_sources(indium-test-encoder-scratch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h" "${CMAKE_CURRENT_BINARY_DIR}/AAPLShaders.h")
target_include_directories(indium-test-encoder-scratch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-encoder-scratch PRIVATE indium_kit indium_private)
set_target_properties(indium-test-encoder-scratch PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
