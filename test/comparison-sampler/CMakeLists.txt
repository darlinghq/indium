project(indium-test-comparison-sampler)

add_executable(indium-test-comparison-sampler comparison-sampler.cpp)

# The module is committed rather than compiled by the build; README.md has the command that produced it.
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/comparison-sampler-spv.h"
	COMMAND xxd -i -n "comparison_sampler_spv" "${CMAKE_CURRENT_SOURCE_DIR}/comparison-sampler.spv" "${CMAKE_CURRENT_BINARY_DIR}/comparison-sampler-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/comparison-sampler.spv"
)
target_sources(indium-test-comparison-sampler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/comparison-sampler-spv.h")

target_include_directories(indium-test-comparison-sampler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-comparison-sampler PRIVATE indium indium_private)
set_target_properties(indium-test-comparison-sampler PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
