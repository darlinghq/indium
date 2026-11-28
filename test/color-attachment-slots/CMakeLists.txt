project(indium-test-color-attachment-slots)

add_executable(indium-test-color-attachment-slots color-attachment-slots.cpp)

# The module is test/fill-mode-depth-clip's (mslc's output for its graphics.metal); modules are committed
# rather than produced by the build, for the reason test/basic-compute-mslc gives.
foreach(module graphics)
	string(REPLACE "-" "_" symbol "${module}")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		COMMAND xxd -i -n "${symbol}_spv" "${CMAKE_CURRENT_SOURCE_DIR}/../fill-mode-depth-clip/${module}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../fill-mode-depth-clip/${module}.spv"
	)
	target_sources(indium-test-color-attachment-slots PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${module}-spv.h")
endforeach()

target_include_directories(indium-test-color-attachment-slots PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-color-attachment-slots PRIVATE indium indium_private)
set_target_properties(indium-test-color-attachment-slots PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
