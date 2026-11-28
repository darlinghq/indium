project(indium-test-round-half-away)

add_executable(indium-test-round-half-away round-half-away.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/round-half-away.h"
	COMMAND xxd -i -n "round_half_away_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/round-half-away.metallib" "${CMAKE_CURRENT_BINARY_DIR}/round-half-away.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/round-half-away.metallib"
)
target_sources(indium-test-round-half-away PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/round-half-away.h")

target_include_directories(indium-test-round-half-away PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-round-half-away PRIVATE indium indium_private)
set_target_properties(indium-test-round-half-away PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME round-half-away-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/round-half-away.metallib" "${CMAKE_CURRENT_BINARY_DIR}/round-half-away.spv")
add_test(NAME round-half-away-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/round-half-away.spv")
set_tests_properties(round-half-away-translate PROPERTIES FIXTURES_SETUP round-half-away-spirv)
set_tests_properties(round-half-away-spirv-val PROPERTIES FIXTURES_REQUIRED round-half-away-spirv)
