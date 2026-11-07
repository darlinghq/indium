project(indium-test-const-bitcast-operand)

add_executable(indium-test-const-bitcast-operand const-bitcast-operand.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/const-bitcast-operand.h"
	COMMAND xxd -i -n "const_bitcast_operand" "${CMAKE_CURRENT_SOURCE_DIR}/const-bitcast-operand.metallib" "${CMAKE_CURRENT_BINARY_DIR}/const-bitcast-operand.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/const-bitcast-operand.metallib"
)
target_sources(indium-test-const-bitcast-operand PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/const-bitcast-operand.h")

target_include_directories(indium-test-const-bitcast-operand PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-const-bitcast-operand PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-const-bitcast-operand
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
