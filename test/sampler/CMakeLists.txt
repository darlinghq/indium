project(indium-test-sampler)

find_path(GLFW_INCLUDE_DIRS GLFW/glfw3.h)
find_library(GLFW_LIBRARIES glfw)

find_package(glm REQUIRED)

add_executable(indium-test-sampler sampler.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/AAPLShaders.h"
	COMMAND xxd -i -n "shader" "${CMAKE_CURRENT_SOURCE_DIR}/AAPLShaders.metallib" "${CMAKE_CURRENT_BINARY_DIR}/AAPLShaders.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/AAPLShaders.metallib"
)
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Image.h"
	COMMAND xxd -i -n "image" "${CMAKE_CURRENT_SOURCE_DIR}/Image.tga" "${CMAKE_CURRENT_BINARY_DIR}/Image.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Image.tga"
)
target_sources(indium-test-sampler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/AAPLShaders.h" "${CMAKE_CURRENT_BINARY_DIR}/Image.h")

target_include_directories(indium-test-sampler PRIVATE
	${GLFW_INCLUDE_DIRS}
	${glm_INCLUDE_DIRS}
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-sampler PRIVATE
	${GLFW_LIBRARIES}
	${glm_LIBRARIES}
	indium_kit
	indium_private
)

set_target_properties(indium-test-sampler
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
