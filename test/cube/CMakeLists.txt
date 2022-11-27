project(indium-test-cube)

find_path(GLFW_INCLUDE_DIRS GLFW/glfw3.h)
find_library(GLFW_LIBRARIES glfw)

find_package(glm REQUIRED)

add_executable(indium-test-cube cube.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	COMMAND xxd -i -n "shaders" "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib" "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib"
)
target_sources(indium-test-cube PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders.h")

target_include_directories(indium-test-cube PRIVATE
	${GLFW_INCLUDE_DIRS}
	${glm_INCLUDE_DIRS}
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-cube PRIVATE
	${GLFW_LIBRARIES}
	${glm_LIBRARIES}
	indium_kit
	indium_private
)

set_target_properties(indium-test-cube
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
