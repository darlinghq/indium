project(indium-test-air-reinterpreted-local)

add_executable(indium-test-air-reinterpreted-local air-reinterpreted-local.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/air-reinterpreted-local.h"
	COMMAND xxd -i -n "air_reinterpreted_local" "${CMAKE_CURRENT_SOURCE_DIR}/air-reinterpreted-local.metallib" "${CMAKE_CURRENT_BINARY_DIR}/air-reinterpreted-local.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/air-reinterpreted-local.metallib"
)
target_sources(indium-test-air-reinterpreted-local PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/air-reinterpreted-local.h")

target_include_directories(indium-test-air-reinterpreted-local PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-air-reinterpreted-local PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-air-reinterpreted-local
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
