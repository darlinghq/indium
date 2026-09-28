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
add_subdirectory(air-ops)
add_subdirectory(texture-readback)

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
add_iridium_check(indium-test-vector-splat vector-splat/vector-splat.cpp)
add_test(NAME vector-splat COMMAND indium-test-vector-splat)
add_iridium_check(indium-test-interface-locations interface-locations/interface-locations.cpp)
add_test(NAME interface-locations COMMAND indium-test-interface-locations)

# init() must report a libLLVM major mismatch with a false return, not an exception (Metal calls it
# from a library constructor); the library the test loads claims to be LLVM 1.
add_library(indium-test-fake-llvm MODULE dynamic-llvm-mismatch/fake-llvm.cpp)
add_executable(indium-test-dynamic-llvm-mismatch dynamic-llvm-mismatch/dynamic-llvm-mismatch.cpp "${INDIUM_ROOT}/src/iridium/dynamic-llvm.cpp")
add_dependencies(indium-test-dynamic-llvm-mismatch indium-test-fake-llvm)
target_include_directories(indium-test-dynamic-llvm-mismatch PRIVATE "${INDIUM_ROOT}/include" "${INDIUM_ROOT}/private-include" ${LLVM_INCLUDE_DIRS})
target_compile_definitions(indium-test-dynamic-llvm-mismatch PRIVATE "HOST_LLVM_LIBNAME=\"$<TARGET_FILE:indium-test-fake-llvm>\"")
target_link_libraries(indium-test-dynamic-llvm-mismatch PRIVATE dl)
set_target_properties(indium-test-dynamic-llvm-mismatch PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME dynamic-llvm-mismatch COMMAND indium-test-dynamic-llvm-mismatch)
# the top-level LLVM discovery block, run against fake llvm-config trees
add_test(NAME llvm-discovery COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/llvm-discovery/check.sh")
