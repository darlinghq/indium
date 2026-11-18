add_executable(indium-test-viewport-origin viewport-origin.cpp)
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/viewport-spv.h"
	COMMAND xxd -i -n viewport_spv "${CMAKE_CURRENT_SOURCE_DIR}/viewport.spv" "${CMAKE_CURRENT_BINARY_DIR}/viewport-spv.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/viewport.spv"
)
target_sources(indium-test-viewport-origin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/viewport-spv.h")
target_include_directories(indium-test-viewport-origin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-viewport-origin PRIVATE indium indium_private)
set_target_properties(indium-test-viewport-origin PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_test(NAME viewport-origin COMMAND indium-test-viewport-origin)
set_tests_properties(viewport-origin PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
