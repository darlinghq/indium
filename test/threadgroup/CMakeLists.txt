project(indium-test-threadgroup)

add_executable(indium-test-threadgroup threadgroup.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/threadgroup.h"
	COMMAND xxd -i -n "threadgroup_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/threadgroup.metallib" "${CMAKE_CURRENT_BINARY_DIR}/threadgroup.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/threadgroup.metallib"
)
target_sources(indium-test-threadgroup PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/threadgroup.h")

target_include_directories(indium-test-threadgroup PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-threadgroup PRIVATE indium indium_private)
set_target_properties(indium-test-threadgroup PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
