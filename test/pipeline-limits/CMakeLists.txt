project(indium-test-pipeline-limits)

add_executable(indium-test-pipeline-limits pipeline-limits.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pipeline-limits.h"
	COMMAND xxd -i -n "pipeline_limits_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/pipeline-limits.metallib" "${CMAKE_CURRENT_BINARY_DIR}/pipeline-limits.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pipeline-limits.metallib"
)
target_sources(indium-test-pipeline-limits PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pipeline-limits.h")

target_include_directories(indium-test-pipeline-limits PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-pipeline-limits PRIVATE indium indium_private)
set_target_properties(indium-test-pipeline-limits PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
