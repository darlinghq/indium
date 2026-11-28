project(indium-test-bool-conversions)

add_executable(indium-test-bool-conversions bool-conversions.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bool-conversions.h"
	COMMAND xxd -i -n "bool_conversions_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/bool-conversions.metallib" "${CMAKE_CURRENT_BINARY_DIR}/bool-conversions.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bool-conversions.metallib"
)
target_sources(indium-test-bool-conversions PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/bool-conversions.h")

target_include_directories(indium-test-bool-conversions PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-bool-conversions PRIVATE indium indium_private)
set_target_properties(indium-test-bool-conversions PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME bool-conversions-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/bool-conversions.metallib" "${CMAKE_CURRENT_BINARY_DIR}/bool-conversions.spv")
add_test(NAME bool-conversions-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/bool-conversions.spv")
set_tests_properties(bool-conversions-translate PROPERTIES FIXTURES_SETUP bool-conversions-spirv)
set_tests_properties(bool-conversions-spirv-val PROPERTIES FIXTURES_REQUIRED bool-conversions-spirv)
