project(indium-test-parallel-render)

add_executable(indium-test-parallel-render parallel-render.cpp)

# The graphics module is the one of test/hazard-tracking (mslc's output for its graphics.metal), turned into a header
# here for the reason test/basic-compute-mslc gives.
set(parallel_module "${CMAKE_CURRENT_SOURCE_DIR}/../hazard-tracking/graphics.spv")
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/graphics-spv.h"
	COMMAND xxd -i -n "graphics_spv" "${parallel_module}" "${CMAKE_CURRENT_BINARY_DIR}/graphics-spv.h"
	DEPENDS "${parallel_module}"
)
target_sources(indium-test-parallel-render PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/graphics-spv.h")
target_include_directories(indium-test-parallel-render PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-parallel-render PRIVATE indium indium_private)
set_target_properties(indium-test-parallel-render PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
