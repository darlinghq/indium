project(indium-test-nonuniform-dispatch)

add_executable(indium-test-nonuniform-dispatch nonuniform-dispatch.cpp)

# The modules are committed rather than produced by the build, for the reason test/basic-compute-mslc
# gives; README.md has the commands.
foreach(module grid barrier)
	string(REPLACE "-" "_" symbol "${module}")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		COMMAND xxd -i -n "${symbol}_spv" "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv"
	)
	target_sources(indium-test-nonuniform-dispatch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h")
endforeach()

target_include_directories(indium-test-nonuniform-dispatch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-nonuniform-dispatch PRIVATE indium indium_private)
set_target_properties(indium-test-nonuniform-dispatch PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
