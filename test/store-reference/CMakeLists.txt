project(indium-test-store-reference)

add_executable(indium-test-store-reference store-reference.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/store-reference.h"
	COMMAND xxd -i -n "store_reference" "${CMAKE_CURRENT_SOURCE_DIR}/store-reference.metallib" "${CMAKE_CURRENT_BINARY_DIR}/store-reference.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/store-reference.metallib"
)
target_sources(indium-test-store-reference PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/store-reference.h")

target_include_directories(indium-test-store-reference PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-store-reference PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-store-reference
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
