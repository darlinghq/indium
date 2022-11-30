project(indium-test-cubemap)

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

add_executable(indium-test-cubemap cubemap.cpp)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	COMMAND xxd -i -n "shaders" "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib" "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders.metallib"
)
target_sources(indium-test-cubemap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders.h")

foreach(TEXTURE IN ITEMS nx ny nz px py pz)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/texture_${TEXTURE}.h"
		COMMAND xxd -i -n "texture_${TEXTURE}" "${CMAKE_CURRENT_SOURCE_DIR}/textures/${TEXTURE}.png" "${CMAKE_CURRENT_BINARY_DIR}/texture_${TEXTURE}.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/textures/${TEXTURE}.png"
	)
	target_sources(indium-test-cubemap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/texture_${TEXTURE}.h")
endforeach()

target_include_directories(indium-test-cubemap PRIVATE
	${GLFW_INCLUDE_DIRS}
	${glm_INCLUDE_DIRS}
	${STB_INCLUDE_DIRS}
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test-cubemap PRIVATE
	${GLFW_LIBRARIES}
	${glm_LIBRARIES}
	indium_kit
	indium_private
)

set_target_properties(indium-test-cubemap
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
