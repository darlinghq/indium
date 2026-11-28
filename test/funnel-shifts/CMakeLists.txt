project(indium-test-funnel-shifts)

add_executable(indium-test-funnel-shifts funnel-shifts.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/funnel-shifts.h"
	COMMAND xxd -i -n "funnel_shifts_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/funnel-shifts.metallib" "${CMAKE_CURRENT_BINARY_DIR}/funnel-shifts.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/funnel-shifts.metallib"
)
target_sources(indium-test-funnel-shifts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/funnel-shifts.h")

target_include_directories(indium-test-funnel-shifts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-funnel-shifts PRIVATE indium indium_private)
set_target_properties(indium-test-funnel-shifts PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# drivers run some invalid SPIR-V without complaint, so the translation is validated too, which needs no device
find_program(SPIRV_VAL spirv-val REQUIRED)
add_test(NAME funnel-shifts-translate COMMAND mtl2spv "${CMAKE_CURRENT_SOURCE_DIR}/funnel-shifts.metallib" "${CMAKE_CURRENT_BINARY_DIR}/funnel-shifts.spv")
add_test(NAME funnel-shifts-spirv-val COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/funnel-shifts.spv")
set_tests_properties(funnel-shifts-translate PROPERTIES FIXTURES_SETUP funnel-shifts-spirv)
set_tests_properties(funnel-shifts-spirv-val PROPERTIES FIXTURES_REQUIRED funnel-shifts-spirv)
