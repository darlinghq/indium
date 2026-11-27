project(indium-test-texture-fetch)

add_executable(indium-test-texture-fetch texture-fetch.cpp)

# The module is committed rather than produced by the build, for the reason test/basic-compute-mslc gives;
# README.md has the command.
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fetch-spv.h"
	COMMAND xxd -i -n "fetch_spv" "${CMAKE_CURRENT_SOURCE_DIR}/fetch.spv" "${CMAKE_CURRENT_BINARY_DIR}/fetch-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fetch.spv"
)
target_sources(indium-test-texture-fetch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fetch-spv.h")

target_include_directories(indium-test-texture-fetch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-texture-fetch PRIVATE indium indium_private)
set_target_properties(indium-test-texture-fetch PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
