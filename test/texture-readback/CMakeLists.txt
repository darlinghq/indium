project(indium-test-texture-readback)

add_executable(indium-test-texture-readback texture-readback.cpp)

target_link_libraries(indium-test-texture-readback PRIVATE
	indium
	indium_private
)

set_target_properties(indium-test-texture-readback
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
