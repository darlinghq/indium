project(indium-test-memory-pool)

add_executable(indium-test-memory-pool memory-pool.cpp)

# the control_flow kernel of air-ops
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h"
	COMMAND xxd -i -n "air_ops_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/../air-ops/air-ops.metallib" "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../air-ops/air-ops.metallib"
)
target_sources(indium-test-memory-pool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h")
target_include_directories(indium-test-memory-pool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-memory-pool PRIVATE indium_kit indium_private)
set_target_properties(indium-test-memory-pool PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
