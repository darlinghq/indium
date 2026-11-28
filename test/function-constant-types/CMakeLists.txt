project(indium-test-function-constant-types)

add_executable(indium-test-function-constant-types function-constant-types.cpp)

# function-constant-types.spv is committed rather than built: it is function-constant-types.metal compiled by
# mslc (https://github.com/darlinghq/mslc, with [[function_constant]] support) in this directory with
#   mslc -V -o function-constant-types.spv function-constant-types.metal
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/function-constant-types-spv.h"
	COMMAND xxd -i -n "function_constant_types_spv" "${CMAKE_CURRENT_SOURCE_DIR}/function-constant-types.spv" "${CMAKE_CURRENT_BINARY_DIR}/function-constant-types-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/function-constant-types.spv"
)
target_sources(indium-test-function-constant-types PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/function-constant-types-spv.h")

target_include_directories(indium-test-function-constant-types PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-function-constant-types PRIVATE
	indium
	indium_private
)

set_target_properties(indium-test-function-constant-types
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
