project(indium-test-float-controls-library)

add_executable(indium-test-float-controls-library float-controls-library.cpp)

# The modules are committed rather than compiled by the build; README.md has the commands that produced them.
foreach(module float32 float16-32-64 plain)
	string(REPLACE "-" "_" symbol "${module}_spv")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		COMMAND xxd -i -n "${symbol}" "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${module}.spv"
	)
	target_sources(indium-test-float-controls-library PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h")
endforeach()

# a module translated from AIR, to show that the check does not touch that path
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/air-add.h"
	COMMAND xxd -i -n "air_add" "${CMAKE_CURRENT_SOURCE_DIR}/../basic-compute/add.metallib" "${CMAKE_CURRENT_BINARY_DIR}/air-add.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../basic-compute/add.metallib"
)
target_sources(indium-test-float-controls-library PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/air-add.h")

target_include_directories(indium-test-float-controls-library PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-float-controls-library PRIVATE indium indium_private)
set_target_properties(indium-test-float-controls-library PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
