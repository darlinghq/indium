project(indium-test-shader-stencil-export)

add_executable(indium-test-shader-stencil-export shader-stencil-export.cpp)

target_link_libraries(indium-test-shader-stencil-export PRIVATE
	indium
	indium_private
)

set_target_properties(indium-test-shader-stencil-export
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
