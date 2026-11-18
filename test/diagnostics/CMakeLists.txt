project(indium-test-diagnostics)

add_executable(indium-test-diagnostics diagnostics.cpp)

# The compute library is the metallib of the basic-compute test, embedded by xxd.
get_filename_component(source "../basic-compute/add.metallib" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compute-lib.h"
	COMMAND xxd -i -n "compute_lib" "${source}" "${CMAKE_CURRENT_BINARY_DIR}/compute-lib.h"
	DEPENDS "${source}"
)
target_sources(indium-test-diagnostics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compute-lib.h")
target_include_directories(indium-test-diagnostics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-diagnostics PRIVATE indium_kit indium_private)
set_target_properties(indium-test-diagnostics PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
