project(indium-test-air-unpack)

add_executable(indium-test-air-unpack air-unpack.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/air-unpack.h"
	COMMAND xxd -i -n "air_unpack_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/air-unpack.metallib" "${CMAKE_CURRENT_BINARY_DIR}/air-unpack.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/air-unpack.metallib"
)
target_sources(indium-test-air-unpack PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/air-unpack.h")

target_include_directories(indium-test-air-unpack PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-air-unpack PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-air-unpack
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
