project(indium-test-air-depth-memcpy)

add_executable(indium-test-air-depth-memcpy air-depth-memcpy.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/air-depth-memcpy.h"
	COMMAND xxd -i -n "air_depth_memcpy" "${CMAKE_CURRENT_SOURCE_DIR}/air-depth-memcpy.metallib" "${CMAKE_CURRENT_BINARY_DIR}/air-depth-memcpy.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/air-depth-memcpy.metallib"
)
target_sources(indium-test-air-depth-memcpy PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/air-depth-memcpy.h")

target_include_directories(indium-test-air-depth-memcpy PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-air-depth-memcpy PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-air-depth-memcpy
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
