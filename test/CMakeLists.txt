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
