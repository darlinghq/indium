project(indium-test-simd-relative-shuffles)

add_executable(indium-test-simd-relative-shuffles simd-relative-shuffles.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/simd-relative-shuffles.h"
	COMMAND xxd -i -n "simd_relative_shuffles_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/simd-relative-shuffles.metallib" "${CMAKE_CURRENT_BINARY_DIR}/simd-relative-shuffles.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/simd-relative-shuffles.metallib"
)
target_sources(indium-test-simd-relative-shuffles PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/simd-relative-shuffles.h")

target_include_directories(indium-test-simd-relative-shuffles PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-simd-relative-shuffles PRIVATE indium indium_private)
set_target_properties(indium-test-simd-relative-shuffles PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME simd-relative-shuffles-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/simd-relative-shuffles.metallib" "${CMAKE_CURRENT_BINARY_DIR}/simd-relative-shuffles.spv")
add_test(NAME simd-relative-shuffles-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/simd-relative-shuffles.spv")
set_tests_properties(simd-relative-shuffles-translate PROPERTIES FIXTURES_SETUP simd-relative-shuffles-spirv)
set_tests_properties(simd-relative-shuffles-spirv-val PROPERTIES FIXTURES_REQUIRED simd-relative-shuffles-spirv)
