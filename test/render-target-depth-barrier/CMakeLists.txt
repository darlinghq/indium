project(indium-test-render-target-depth-barrier)

add_executable(indium-test-render-target-depth-barrier render-target-depth-barrier.cpp)

# The module is committed rather than produced by the build, for the reason test/basic-compute-mslc gives;
# README.md has the command.
foreach(module graphics)
	string(REPLACE "-" "_" symbol "${module}")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		COMMAND xxd -i -n "${symbol}_spv" "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv"
	)
	target_sources(indium-test-render-target-depth-barrier PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h")
endforeach()

target_include_directories(indium-test-render-target-depth-barrier PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-render-target-depth-barrier PRIVATE indium indium_private)
set_target_properties(indium-test-render-target-depth-barrier PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(NAME render-target-depth-barrier COMMAND indium-test-render-target-depth-barrier)
