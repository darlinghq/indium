project(indium-test-visibility-results)

add_executable(indium-test-visibility-results visibility-results.cpp)

# The module is committed rather than produced by the build, for the reason test/basic-compute-mslc
# gives; README.md has the command.
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/graphics-spv.h"
	COMMAND xxd -i -n "graphics_spv" "${CMAKE_CURRENT_SOURCE_DIR}/graphics.spv" "${CMAKE_CURRENT_BINARY_DIR}/graphics-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/graphics.spv"
)
target_sources(indium-test-visibility-results PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/graphics-spv.h")

target_include_directories(indium-test-visibility-results PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-visibility-results PRIVATE indium indium_private)
set_target_properties(indium-test-visibility-results PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
