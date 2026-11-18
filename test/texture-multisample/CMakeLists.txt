project(indium-test-texture-multisample)

add_executable(indium-test-texture-multisample texture-multisample.cpp)

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
foreach(shader half-plane.vert paint.frag paint-uint.frag)
	string(REGEX REPLACE "[.-]" "_" symbol "${shader}")
	add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${shader}.h"
		COMMAND "${GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -o "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv"
		COMMAND xxd -i -n "${symbol}" "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${shader}.h"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}")
	target_sources(indium-test-texture-multisample PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${shader}.h")
endforeach()

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/texture-multisample.h"
	COMMAND xxd -i -n "texture_multisample_metallib" "${CMAKE_CURRENT_SOURCE_DIR}/texture-multisample.metallib" "${CMAKE_CURRENT_BINARY_DIR}/texture-multisample.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/texture-multisample.metallib"
)
target_sources(indium-test-texture-multisample PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/texture-multisample.h")

target_include_directories(indium-test-texture-multisample PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(indium-test-texture-multisample PRIVATE indium indium_private)
set_target_properties(indium-test-texture-multisample PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
