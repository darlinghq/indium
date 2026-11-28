project(indium-test-kernel-input-attributes)

add_executable(indium-test-kernel-input-attributes kernel-input-attributes.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/kernel-input-attributes.h"
	COMMAND xxd -i -n "kernel_input_attributes_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/kernel-input-attributes.metallib" "${CMAKE_CURRENT_BINARY_DIR}/kernel-input-attributes.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/kernel-input-attributes.metallib"
)
target_sources(indium-test-kernel-input-attributes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/kernel-input-attributes.h")

target_include_directories(indium-test-kernel-input-attributes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-kernel-input-attributes PRIVATE indium indium_private)
set_target_properties(indium-test-kernel-input-attributes PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME kernel-input-attributes-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/kernel-input-attributes.metallib" "${CMAKE_CURRENT_BINARY_DIR}/kernel-input-attributes.spv")
add_test(NAME kernel-input-attributes-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/kernel-input-attributes.spv")
set_tests_properties(kernel-input-attributes-translate PROPERTIES FIXTURES_SETUP kernel-input-attributes-spirv)
set_tests_properties(kernel-input-attributes-spirv-val PROPERTIES FIXTURES_REQUIRED kernel-input-attributes-spirv)
