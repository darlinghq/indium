project(indium-test-min-max3)

add_executable(indium-test-min-max3 min-max3.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/min-max3.h"
	COMMAND xxd -i -n "min_max3_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/min-max3.metallib" "${CMAKE_CURRENT_BINARY_DIR}/min-max3.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/min-max3.metallib"
)
target_sources(indium-test-min-max3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/min-max3.h")

target_include_directories(indium-test-min-max3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-min-max3 PRIVATE indium indium_private)
set_target_properties(indium-test-min-max3 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME min-max3-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/min-max3.metallib" "${CMAKE_CURRENT_BINARY_DIR}/min-max3.spv")
add_test(NAME min-max3-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/min-max3.spv")
set_tests_properties(min-max3-translate PROPERTIES FIXTURES_SETUP min-max3-spirv)
set_tests_properties(min-max3-spirv-val PROPERTIES FIXTURES_REQUIRED min-max3-spirv)
