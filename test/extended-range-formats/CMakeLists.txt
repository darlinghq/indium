add_executable(indium-test-extended-range-formats extended-range-formats.cpp)
target_link_libraries(indium-test-extended-range-formats PRIVATE indium indium_private)
set_target_properties(indium-test-extended-range-formats PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
# name, source, defines
set(shaders
    "vertex|fullscreen.vert|"
    "fill|fill.frag|"
    "blend|blend.frag|"
    "sample_nearest|sample.comp|"
    "sample_halfway|sample.comp|-DHALFWAY")
foreach(entry IN LISTS shaders)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 source)
    list(GET parts 2 defines)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${name}-shader.h"
        COMMAND "${GLSLANG_VALIDATOR}" -V ${defines} -I"${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${source}" -o "${CMAKE_CURRENT_BINARY_DIR}/${name}.spv"
        COMMAND xxd -i -n "${name}_shader" "${CMAKE_CURRENT_BINARY_DIR}/${name}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${name}-shader.h"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}" "${CMAKE_CURRENT_SOURCE_DIR}/fill-values.glsl")
    target_sources(indium-test-extended-range-formats PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${name}-shader.h")
endforeach()
target_include_directories(indium-test-extended-range-formats PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
