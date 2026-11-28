project(indium-test-wide-vector-bitcast)

add_executable(indium-test-wide-vector-bitcast wide-vector-bitcast.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wide-vector-bitcast.h"
	COMMAND xxd -i -n "wide_vector_bitcast_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/wide-vector-bitcast.metallib" "${CMAKE_CURRENT_BINARY_DIR}/wide-vector-bitcast.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wide-vector-bitcast.metallib"
)
target_sources(indium-test-wide-vector-bitcast PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/wide-vector-bitcast.h")

target_include_directories(indium-test-wide-vector-bitcast PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-wide-vector-bitcast PRIVATE indium indium_private)
set_target_properties(indium-test-wide-vector-bitcast PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME wide-vector-bitcast-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/wide-vector-bitcast.metallib" "${CMAKE_CURRENT_BINARY_DIR}/wide-vector-bitcast.spv")
add_test(NAME wide-vector-bitcast-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/wide-vector-bitcast.spv")
set_tests_properties(wide-vector-bitcast-translate PROPERTIES FIXTURES_SETUP wide-vector-bitcast-spirv)
set_tests_properties(wide-vector-bitcast-spirv-val PROPERTIES FIXTURES_REQUIRED wide-vector-bitcast-spirv)
