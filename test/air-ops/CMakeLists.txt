project(indium-test-air-ops)

add_executable(indium-test-air-ops air-ops.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h"
	COMMAND xxd -i -n "air_ops_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/air-ops.metallib" "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/air-ops.metallib"
)
target_sources(indium-test-air-ops PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/air-ops.h")

target_include_directories(indium-test-air-ops PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-air-ops PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-air-ops
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
