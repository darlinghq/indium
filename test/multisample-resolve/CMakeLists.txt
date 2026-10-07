add_executable(indium-test-multisample-resolve multisample-resolve.cpp)
target_link_libraries(indium-test-multisample-resolve PRIVATE indium indium_private)
set_target_properties(indium-test-multisample-resolve PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
foreach(stage vertex fragment)
    if(stage STREQUAL "vertex")
        set(shader_source fullscreen.vert)
    else()
        set(shader_source blue.frag)
    endif()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${stage}-shader.h"
        COMMAND "${GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/${shader_source}" -o "${CMAKE_CURRENT_BINARY_DIR}/${stage}.spv"
        COMMAND xxd -i -n "${stage}_shader" "${CMAKE_CURRENT_BINARY_DIR}/${stage}.spv" "${CMAKE_CURRENT_BINARY_DIR}/${stage}-shader.h"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader_source}")
    target_sources(indium-test-multisample-resolve PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${stage}-shader.h")
endforeach()
target_include_directories(indium-test-multisample-resolve PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
