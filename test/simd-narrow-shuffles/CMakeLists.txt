project(indium-test-simd-narrow-shuffles)

add_executable(indium-test-simd-narrow-shuffles simd-narrow-shuffles.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/simd-narrow-shuffles.h"
	COMMAND xxd -i -n "simd_narrow_shuffles_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/simd-narrow-shuffles.metallib" "${CMAKE_CURRENT_BINARY_DIR}/simd-narrow-shuffles.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/simd-narrow-shuffles.metallib"
)
target_sources(indium-test-simd-narrow-shuffles PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/simd-narrow-shuffles.h")

target_include_directories(indium-test-simd-narrow-shuffles PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-simd-narrow-shuffles PRIVATE indium indium_private)
set_target_properties(indium-test-simd-narrow-shuffles PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME simd-narrow-shuffles-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/simd-narrow-shuffles.metallib" "${CMAKE_CURRENT_BINARY_DIR}/simd-narrow-shuffles.spv")
add_test(NAME simd-narrow-shuffles-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/simd-narrow-shuffles.spv")
set_tests_properties(simd-narrow-shuffles-translate PROPERTIES FIXTURES_SETUP simd-narrow-shuffles-spirv)
set_tests_properties(simd-narrow-shuffles-spirv-val PROPERTIES FIXTURES_REQUIRED simd-narrow-shuffles-spirv)
