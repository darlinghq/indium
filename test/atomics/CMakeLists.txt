project(indium-test-atomics)

add_executable(indium-test-atomics atomics.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/atomics.h"
	COMMAND xxd -i -n "atomics_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/atomics.metallib" "${CMAKE_CURRENT_BINARY_DIR}/atomics.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/atomics.metallib"
)
target_sources(indium-test-atomics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/atomics.h")

target_include_directories(indium-test-atomics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-atomics PRIVATE indium indium_private)
set_target_properties(indium-test-atomics PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
