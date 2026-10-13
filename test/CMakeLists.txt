project(indium-test)

add_subdirectory(triangle)
add_subdirectory(sampler)
add_subdirectory(cube)
add_subdirectory(lighting)
add_subdirectory(texturing)
add_subdirectory(cubemap)
add_subdirectory(basic-compute)
add_subdirectory(store-reference)
add_subdirectory(const-bitcast-operand)

# cube, cubemap, lighting and texturing include <glm/gtx/transform.hpp> and
# <glm/gtx/type_aligned.hpp>, and GLM 1.0.3 hard-errors in every gtx header
# unless GLM_ENABLE_EXPERIMENTAL is defined. This is toolchain drift against
# whichever GLM is installed, not an indium defect: the define belongs to these
# fixtures' own use of gtx, and any other consumer of gtx needs it the same way.
foreach(target indium-test-cube indium-test-cubemap indium-test-lighting indium-test-texturing)
	target_compile_definitions(${target} PRIVATE GLM_ENABLE_EXPERIMENTAL)
endforeach()


# Standalone translator checks: each includes src/iridium/air.cpp and authors its input
# through LLVM's C API, so it links libLLVM directly. physical-gep compares disassembly and
# validates modules, so ENABLE_TESTS requires SPIRV-Tools rather than skipping that check.
find_program(SPIRV_DIS spirv-dis REQUIRED)
find_program(SPIRV_VAL spirv-val REQUIRED)
get_filename_component(INDIUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
function(add_iridium_check target source)
	add_executable(${target} ${source} "${INDIUM_ROOT}/src/iridium/spirv.cpp" "${INDIUM_ROOT}/src/iridium/dynamic-llvm.cpp")
	target_include_directories(${target} PRIVATE "${INDIUM_ROOT}/include" "${INDIUM_ROOT}/private-include" ${LLVM_INCLUDE_DIRS})
	target_compile_definitions(${target} PRIVATE "HOST_LLVM_LIBNAME=\"${HOST_LLVM_LIBNAME}\"")
	target_link_libraries(${target} PRIVATE LLVM dl)
	set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endfunction()

add_iridium_check(indium-test-physical-gep physical-gep/physical-gep.cpp)
add_test(NAME physical-gep
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/physical-gep/check.sh" $<TARGET_FILE:indium-test-physical-gep> "${CMAKE_CURRENT_BINARY_DIR}/physical-gep-out")
add_iridium_check(indium-test-vector-types vector-layout/vector-types.cpp)
add_test(NAME vector-types COMMAND indium-test-vector-types)
# built so a Type constructor change cannot silently break it; its spirv-val step is manual
add_iridium_check(indium-test-numeric-constants numeric-constants/numeric-constants.cpp)
