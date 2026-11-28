project(indium-test-barrier-semantics)

add_executable(indium-test-barrier-semantics barrier-semantics.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/barrier-semantics.h"
	COMMAND xxd -i -n "barrier_semantics_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/barrier-semantics.metallib" "${CMAKE_CURRENT_BINARY_DIR}/barrier-semantics.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/barrier-semantics.metallib"
)
target_sources(indium-test-barrier-semantics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/barrier-semantics.h")

target_include_directories(indium-test-barrier-semantics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-barrier-semantics PRIVATE indium indium_private)
set_target_properties(indium-test-barrier-semantics PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME barrier-semantics-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/barrier-semantics.metallib" "${CMAKE_CURRENT_BINARY_DIR}/barrier-semantics.spv")
add_test(NAME barrier-semantics-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/barrier-semantics.spv")
set_tests_properties(barrier-semantics-translate PROPERTIES FIXTURES_SETUP barrier-semantics-spirv)
set_tests_properties(barrier-semantics-spirv-val PROPERTIES FIXTURES_REQUIRED barrier-semantics-spirv)
