project(indium-test-basic-compute)

find_path(GLFW_INCLUDE_DIRS GLFW/glfw3.h)
find_library(GLFW_LIBRARIES glfw)

find_package(glm REQUIRED)

add_executable(indium-test-basic-compute basic-compute.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/add.h"
	COMMAND xxd -i -n "compute_add" "${CMAKE_CURRENT_SOURCE_DIR}/add.metallib" "${CMAKE_CURRENT_BINARY_DIR}/add.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/add.metallib"
)
target_sources(indium-test-basic-compute PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/add.h")

target_include_directories(indium-test-basic-compute PRIVATE
	${GLFW_INCLUDE_DIRS}
	${glm_INCLUDE_DIRS}
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-basic-compute PRIVATE
	${GLFW_LIBRARIES}
	${glm_LIBRARIES}
	indium_kit
	indium_private
)

set_target_properties(indium-test-basic-compute
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
