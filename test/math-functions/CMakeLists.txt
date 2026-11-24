project(indium-test-math-functions)

add_executable(indium-test-math-functions math-functions.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/math-functions.h"
	COMMAND xxd -i -n "math_functions_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/math-functions.metallib" "${CMAKE_CURRENT_BINARY_DIR}/math-functions.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/math-functions.metallib"
)
target_sources(indium-test-math-functions PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/math-functions.h")

target_include_directories(indium-test-math-functions PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-math-functions PRIVATE indium indium_private)
set_target_properties(indium-test-math-functions PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME math-functions-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/math-functions.metallib" "${CMAKE_CURRENT_BINARY_DIR}/math-functions.spv")
add_test(NAME math-functions-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/math-functions.spv")
set_tests_properties(math-functions-translate PROPERTIES FIXTURES_SETUP math-functions-spirv)
set_tests_properties(math-functions-spirv-val PROPERTIES FIXTURES_REQUIRED math-functions-spirv)
