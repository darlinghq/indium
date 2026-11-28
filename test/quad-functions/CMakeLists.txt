project(indium-test-quad-functions)

add_executable(indium-test-quad-functions quad-functions.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/quad-functions.h"
	COMMAND xxd -i -n "quad_functions_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/quad-functions.metallib" "${CMAKE_CURRENT_BINARY_DIR}/quad-functions.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/quad-functions.metallib"
)
target_sources(indium-test-quad-functions PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/quad-functions.h")

target_include_directories(indium-test-quad-functions PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-quad-functions PRIVATE indium indium_private)
set_target_properties(indium-test-quad-functions PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME quad-functions-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/quad-functions.metallib" "${CMAKE_CURRENT_BINARY_DIR}/quad-functions.spv")
add_test(NAME quad-functions-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/quad-functions.spv")
set_tests_properties(quad-functions-translate PROPERTIES FIXTURES_SETUP quad-functions-spirv)
set_tests_properties(quad-functions-spirv-val PROPERTIES FIXTURES_REQUIRED quad-functions-spirv)
