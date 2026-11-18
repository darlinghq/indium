project(indium-test-simdgroup)

add_executable(indium-test-simdgroup simdgroup.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/simdgroup.h"
	COMMAND xxd -i -n "simdgroup_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/simdgroup.metallib" "${CMAKE_CURRENT_BINARY_DIR}/simdgroup.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/simdgroup.metallib"
)
target_sources(indium-test-simdgroup PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/simdgroup.h")

target_include_directories(indium-test-simdgroup PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-simdgroup PRIVATE indium indium_private)
set_target_properties(indium-test-simdgroup PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
