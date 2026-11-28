project(indium-test-gated-texture-aliases)

add_executable(indium-test-gated-texture-aliases gated-texture-aliases.cpp)

# The module is committed rather than compiled by the build; README.md has the command that produced it.
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gated-texture-aliases-spv.h"
	COMMAND xxd -i -n "gated_texture_aliases_spv" "${CMAKE_CURRENT_SOURCE_DIR}/gated-texture-aliases.spv" "${CMAKE_CURRENT_BINARY_DIR}/gated-texture-aliases-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gated-texture-aliases.spv"
)
target_sources(indium-test-gated-texture-aliases PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gated-texture-aliases-spv.h")

target_include_directories(indium-test-gated-texture-aliases PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-gated-texture-aliases PRIVATE indium indium_private)
set_target_properties(indium-test-gated-texture-aliases PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
