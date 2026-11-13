project(indium-test-function-constants-gpu)

add_executable(indium-test-function-constants-gpu function-constants-gpu.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/function-constants.h"
	COMMAND xxd -i -n "function_constants_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/function-constants.metallib" "${CMAKE_CURRENT_BINARY_DIR}/function-constants.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/function-constants.metallib"
)
target_sources(indium-test-function-constants-gpu PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/function-constants.h")

target_include_directories(indium-test-function-constants-gpu PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-function-constants-gpu PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-function-constants-gpu
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
