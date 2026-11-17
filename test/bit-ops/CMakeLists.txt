project(indium-test-bit-ops)

add_executable(indium-test-bit-ops bit-ops.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bit-ops.h"
	COMMAND xxd -i -n "bit_ops_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/bit-ops.metallib" "${CMAKE_CURRENT_BINARY_DIR}/bit-ops.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bit-ops.metallib"
)
target_sources(indium-test-bit-ops PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/bit-ops.h")

target_include_directories(indium-test-bit-ops PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-bit-ops PRIVATE indium indium_private)
set_target_properties(indium-test-bit-ops PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
