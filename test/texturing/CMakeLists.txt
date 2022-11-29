project(indium-test-texturing)

find_path(GLFW_INCLUDE_DIRS GLFW/glfw3.h)
find_library(GLFW_LIBRARIES glfw)

find_path(STB_INCLUDE_DIRS
	NAMES
		stb_image.h
	PATH_SUFFIXES
		stb
	REQUIRED
)

find_package(glm REQUIRED)

add_executable(indium-test-texturing texturing.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	COMMAND xxd -i -n "shaders" "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib" "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib"
)
target_sources(indium-test-texturing PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders.h")

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/spot.h"
	COMMAND xxd -i -n "spot" "${CMAKE_CURRENT_SOURCE_DIR}/spot.obj" "${CMAKE_CURRENT_BINARY_DIR}/spot.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/spot.obj"
)
target_sources(indium-test-texturing PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/spot.h")

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/spot_texture.h"
	COMMAND xxd -i -n "spot_texture" "${CMAKE_CURRENT_SOURCE_DIR}/spot_texture.png" "${CMAKE_CURRENT_BINARY_DIR}/spot_texture.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/spot_texture.png"
)
target_sources(indium-test-texturing PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/spot_texture.h")

target_include_directories(indium-test-texturing PRIVATE
	${GLFW_INCLUDE_DIRS}
	${glm_INCLUDE_DIRS}
	${STB_INCLUDE_DIRS}
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-texturing PRIVATE
	${GLFW_LIBRARIES}
	${glm_LIBRARIES}
	indium_kit
	indium_private
)

set_target_properties(indium-test-texturing
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
