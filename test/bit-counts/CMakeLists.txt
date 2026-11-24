project(indium-test-bit-counts)

add_executable(indium-test-bit-counts bit-counts.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bit-counts.h"
	COMMAND xxd -i -n "bit_counts_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/bit-counts.metallib" "${CMAKE_CURRENT_BINARY_DIR}/bit-counts.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bit-counts.metallib"
)
target_sources(indium-test-bit-counts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/bit-counts.h")

target_include_directories(indium-test-bit-counts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-bit-counts PRIVATE indium indium_private)
set_target_properties(indium-test-bit-counts PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME bit-counts-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/bit-counts.metallib" "${CMAKE_CURRENT_BINARY_DIR}/bit-counts.spv")
add_test(NAME bit-counts-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/bit-counts.spv")
set_tests_properties(bit-counts-translate PROPERTIES FIXTURES_SETUP bit-counts-spirv)
set_tests_properties(bit-counts-spirv-val PROPERTIES FIXTURES_REQUIRED bit-counts-spirv)
