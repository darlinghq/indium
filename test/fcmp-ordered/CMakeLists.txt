project(indium-test-fcmp-ordered)

add_executable(indium-test-fcmp-ordered fcmp-ordered.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fcmp-ordered.h"
	COMMAND xxd -i -n "fcmp_ordered_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/fcmp-ordered.metallib" "${CMAKE_CURRENT_BINARY_DIR}/fcmp-ordered.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fcmp-ordered.metallib"
)
target_sources(indium-test-fcmp-ordered PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fcmp-ordered.h")

target_include_directories(indium-test-fcmp-ordered PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-fcmp-ordered PRIVATE indium indium_private)
set_target_properties(indium-test-fcmp-ordered PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME fcmp-ordered-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/fcmp-ordered.metallib" "${CMAKE_CURRENT_BINARY_DIR}/fcmp-ordered.spv")
add_test(NAME fcmp-ordered-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/fcmp-ordered.spv")
set_tests_properties(fcmp-ordered-translate PROPERTIES FIXTURES_SETUP fcmp-ordered-spirv)
set_tests_properties(fcmp-ordered-spirv-val PROPERTIES FIXTURES_REQUIRED fcmp-ordered-spirv)
