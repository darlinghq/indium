project(indium-test-sampler-lod)

add_executable(indium-test-sampler-lod sampler-lod.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sampler-lod.h"
	COMMAND xxd -i -n "sampler_lod" "${CMAKE_CURRENT_SOURCE_DIR}/sampler-lod.metallib" "${CMAKE_CURRENT_BINARY_DIR}/sampler-lod.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sampler-lod.metallib"
)
target_sources(indium-test-sampler-lod PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/sampler-lod.h")

target_include_directories(indium-test-sampler-lod PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-sampler-lod PRIVATE
	indium_kit
	indium_private
)

set_target_properties(indium-test-sampler-lod
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
