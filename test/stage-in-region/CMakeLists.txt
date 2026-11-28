project(indium-test-stage-in-region)

add_executable(indium-test-stage-in-region stage-in-region.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/stage-in-region.h"
	COMMAND xxd -i -n "stage_in_region_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/stage-in-region.metallib" "${CMAKE_CURRENT_BINARY_DIR}/stage-in-region.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stage-in-region.metallib"
)
target_sources(indium-test-stage-in-region PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/stage-in-region.h")

target_include_directories(indium-test-stage-in-region PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-stage-in-region PRIVATE indium indium_private)
set_target_properties(indium-test-stage-in-region PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME stage-in-region-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/stage-in-region.metallib" "${CMAKE_CURRENT_BINARY_DIR}/stage-in-region.spv")
add_test(NAME stage-in-region-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/stage-in-region.spv")
set_tests_properties(stage-in-region-translate PROPERTIES FIXTURES_SETUP stage-in-region-spirv)
set_tests_properties(stage-in-region-spirv-val PROPERTIES FIXTURES_REQUIRED stage-in-region-spirv)
