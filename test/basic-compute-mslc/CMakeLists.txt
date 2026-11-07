project(indium-test-basic-compute-mslc)

add_executable(indium-test-basic-compute-mslc basic-compute-mslc.cpp)

# The module and its reflection are committed rather than produced by the build:
# mslc is a separate repository under concurrent development, and a test that
# generated its own input would be testing whatever mslc happened to emit that
# day. README.md has the command that produced them.
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/add-mslc-spv.h"
	COMMAND xxd -i -n "add_mslc_spv" "${CMAKE_CURRENT_SOURCE_DIR}/add-mslc.spv" "${CMAKE_CURRENT_BINARY_DIR}/add-mslc-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/add-mslc.spv"
)
target_sources(indium-test-basic-compute-mslc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/add-mslc-spv.h")

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/add-mslc-reflect.h"
	COMMAND xxd -i -n "add_mslc_reflect" "${CMAKE_CURRENT_SOURCE_DIR}/add-mslc.reflect.json" "${CMAKE_CURRENT_BINARY_DIR}/add-mslc-reflect.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/add-mslc.reflect.json"
)
target_sources(indium-test-basic-compute-mslc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/add-mslc-reflect.h")

target_include_directories(indium-test-basic-compute-mslc PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-basic-compute-mslc PRIVATE
	indium
	indium_private
)

set_target_properties(indium-test-basic-compute-mslc
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
