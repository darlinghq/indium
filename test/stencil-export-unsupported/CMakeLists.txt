project(indium-test-stencil-export-unsupported)

add_executable(indium-test-stencil-export-unsupported stencil-export-unsupported.cpp)

# The modules are committed rather than compiled by the build; README.md has the commands that produced them.
foreach(module stencil-export-unsupported late-private-pointer copied-pointer)
	string(REPLACE "-" "_" symbol "${module}_spv")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		COMMAND xxd -i -n "${symbol}" "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv"
	)
	target_sources(indium-test-stencil-export-unsupported PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h")
endforeach()

target_include_directories(indium-test-stencil-export-unsupported PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-stencil-export-unsupported PRIVATE indium indium_private)
set_target_properties(indium-test-stencil-export-unsupported PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
