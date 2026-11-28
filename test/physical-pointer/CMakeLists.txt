project(indium-test-physical-pointer)

add_executable(indium-test-physical-pointer physical-pointer.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/physical-pointer.h"
	COMMAND xxd -i -n "physical_pointer_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/physical-pointer.metallib" "${CMAKE_CURRENT_BINARY_DIR}/physical-pointer.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/physical-pointer.metallib"
)
target_sources(indium-test-physical-pointer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/physical-pointer.h")

target_include_directories(indium-test-physical-pointer PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-physical-pointer PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-physical-pointer
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
