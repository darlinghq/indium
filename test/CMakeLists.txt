project(indium-test)

find_path(GLFW_INCLUDE_DIRS GLFW/glfw3.h)
find_library(GLFW_LIBRARIES glfw)

find_package(glm REQUIRED)

add_executable(indium-test test.cpp)

foreach(STAGE vert frag)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shader.${STAGE}.spv"
		COMMAND glslc "${CMAKE_CURRENT_SOURCE_DIR}/shader.${STAGE}" -o "${CMAKE_CURRENT_BINARY_DIR}/shader.${STAGE}.spv"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shader.${STAGE}"
		IMPLICIT_DEPENDS CXX "${CMAKE_CURRENT_SOURCE_DIR}/shader.${STAGE}"
	)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shader.${STAGE}.h"
		COMMAND xxd -i -n "shader_${STAGE}" "${CMAKE_CURRENT_BINARY_DIR}/shader.${STAGE}.spv" "${CMAKE_CURRENT_BINARY_DIR}/shader.${STAGE}.h"
		DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/shader.${STAGE}.spv"
	)
	target_sources(indium-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shader.${STAGE}.h")
endforeach()

target_include_directories(indium-test PRIVATE
	${GLFW_INCLUDE_DIRS}
	${glm_INCLUDE_DIRS}
	"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(indium-test PRIVATE
	${GLFW_LIBRARIES}
	${glm_LIBRARIES}
	indium_kit
	indium_private
)

set_target_properties(indium-test
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
