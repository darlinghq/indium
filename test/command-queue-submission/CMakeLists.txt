project(indium-test-command-queue-submission)

add_executable(indium-test-command-queue-submission command-queue-submission.cpp)

# The compute module is the one of test/hazard-tracking (mslc's output for its compute.metal; the test runs its `step`
# kernel), turned into a header here for the reason test/basic-compute-mslc gives.
set(submission_module "${CMAKE_CURRENT_SOURCE_DIR}/../hazard-tracking/compute.spv")
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compute-spv.h"
	COMMAND xxd -i -n "compute_spv" "${submission_module}" "${CMAKE_CURRENT_BINARY_DIR}/compute-spv.h"
	DEPENDS "${submission_module}"
)
target_sources(indium-test-command-queue-submission PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compute-spv.h")
target_include_directories(indium-test-command-queue-submission PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-command-queue-submission PRIVATE indium indium_private)
set_target_properties(indium-test-command-queue-submission PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
