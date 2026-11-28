project(indium-test-narrow-builtins)

add_executable(indium-test-narrow-builtins narrow-builtins.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/narrow-builtins.h"
	COMMAND xxd -i -n "narrow_builtins_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/narrow-builtins.metallib" "${CMAKE_CURRENT_BINARY_DIR}/narrow-builtins.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/narrow-builtins.metallib"
)
target_sources(indium-test-narrow-builtins PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/narrow-builtins.h")

target_include_directories(indium-test-narrow-builtins PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-narrow-builtins PRIVATE indium indium_private)
set_target_properties(indium-test-narrow-builtins PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME narrow-builtins-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/narrow-builtins.metallib" "${CMAKE_CURRENT_BINARY_DIR}/narrow-builtins.spv")
add_test(NAME narrow-builtins-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/narrow-builtins.spv")
set_tests_properties(narrow-builtins-translate PROPERTIES FIXTURES_SETUP narrow-builtins-spirv)
set_tests_properties(narrow-builtins-spirv-val PROPERTIES FIXTURES_REQUIRED narrow-builtins-spirv)
