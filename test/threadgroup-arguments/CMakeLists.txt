project(indium-test-threadgroup-arguments)

add_executable(indium-test-threadgroup-arguments threadgroup-arguments.cpp)

# The module is committed rather than produced by the build, for the reason test/basic-compute-mslc gives;
# README.md has the command.
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/threadgroup-arguments-spv.h"
	COMMAND xxd -i -n "threadgroup_arguments_spv" "${CMAKE_CURRENT_SOURCE_DIR}/threadgroup-arguments.spv" "${CMAKE_CURRENT_BINARY_DIR}/threadgroup-arguments-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/threadgroup-arguments.spv"
)
target_sources(indium-test-threadgroup-arguments PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/threadgroup-arguments-spv.h")

target_include_directories(indium-test-threadgroup-arguments PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-threadgroup-arguments PRIVATE indium indium_private)
set_target_properties(indium-test-threadgroup-arguments PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
