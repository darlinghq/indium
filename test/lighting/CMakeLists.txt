project(indium-test-lighting)

find_path(GLFW_INCLUDE_DIRS GLFW/glfw3.h)
find_library(GLFW_LIBRARIES glfw)

find_package(glm REQUIRED)

add_executable(indium-test-lighting lighting.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	COMMAND xxd -i -n "shaders" "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib" "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib"
)
target_sources(indium-test-lighting PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders.h")

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/teapot.h"
	COMMAND xxd -i -n "teapot" "${CMAKE_CURRENT_SOURCE_DIR}/teapot.obj" "${CMAKE_CURRENT_BINARY_DIR}/teapot.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/teapot.obj"
)
target_sources(indium-test-lighting PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/teapot.h")

target_include_directories(indium-test-lighting PRIVATE
	${GLFW_INCLUDE_DIRS}
	${glm_INCLUDE_DIRS}
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-lighting PRIVATE
	${GLFW_LIBRARIES}
	${glm_LIBRARIES}
	indium_kit
	indium_private
)

set_target_properties(indium-test-lighting
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
