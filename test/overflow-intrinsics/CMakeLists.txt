project(indium-test-overflow-intrinsics)

add_executable(indium-test-overflow-intrinsics overflow-intrinsics.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/overflow-intrinsics.h"
	COMMAND xxd -i -n "overflow_intrinsics_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/overflow-intrinsics.metallib" "${CMAKE_CURRENT_BINARY_DIR}/overflow-intrinsics.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/overflow-intrinsics.metallib"
)
target_sources(indium-test-overflow-intrinsics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/overflow-intrinsics.h")

target_include_directories(indium-test-overflow-intrinsics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-overflow-intrinsics PRIVATE indium indium_private)
set_target_properties(indium-test-overflow-intrinsics PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME overflow-intrinsics-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/overflow-intrinsics.metallib" "${CMAKE_CURRENT_BINARY_DIR}/overflow-intrinsics.spv")
add_test(NAME overflow-intrinsics-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/overflow-intrinsics.spv")
set_tests_properties(overflow-intrinsics-translate PROPERTIES FIXTURES_SETUP overflow-intrinsics-spirv)
set_tests_properties(overflow-intrinsics-spirv-val PROPERTIES FIXTURES_REQUIRED overflow-intrinsics-spirv)
