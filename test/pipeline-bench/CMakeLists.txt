project(indium-test-pipeline-bench)

add_executable(indium-test-pipeline-bench pipeline-bench.cpp)

# The libraries are the metallibs of the lighting and basic-compute tests, embedded by xxd.
foreach(module lighting:../lighting/shaders.metallib compute:../basic-compute/add.metallib)
	string(REPLACE ":" ";" parts "${module}")
	list(GET parts 0 name)
	list(GET parts 1 path)
	get_filename_component(source "${path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${name}-lib.h"
		COMMAND xxd -i -n "${name}_lib" "${source}" "${CMAKE_CURRENT_BINARY_DIR}/${name}-lib.h"
		DEPENDS "${source}"
	)
	target_sources(indium-test-pipeline-bench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${name}-lib.h")
endforeach()

target_include_directories(indium-test-pipeline-bench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-pipeline-bench PRIVATE indium_kit indium_private)
set_target_properties(indium-test-pipeline-bench PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
