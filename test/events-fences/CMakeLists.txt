project(indium-test-events-fences)

add_executable(indium-test-events-fences events-fences.cpp)

# The compute module is the one test/indirect-draw-dispatch commits (mslc's output for its compute.metal; the
# test only runs its `mark` kernel), turned into a header here for the reason test/basic-compute-mslc gives.
set(events_fences_module "${CMAKE_CURRENT_SOURCE_DIR}/../indirect-draw-dispatch/compute.spv")
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compute-spv.h"
	COMMAND xxd -i -n "compute_spv" "${events_fences_module}" "${CMAKE_CURRENT_BINARY_DIR}/compute-spv.h"
	DEPENDS "${events_fences_module}"
)
target_sources(indium-test-events-fences PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compute-spv.h")
target_include_directories(indium-test-events-fences PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-events-fences PRIVATE indium indium_private)
set_target_properties(indium-test-events-fences PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
