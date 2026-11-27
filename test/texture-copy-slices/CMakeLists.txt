project(indium-test-texture-copy-slices)

add_executable(indium-test-texture-copy-slices texture-copy-slices.cpp)

target_link_libraries(indium-test-texture-copy-slices PRIVATE
	indium
	indium_private
)

set_target_properties(indium-test-texture-copy-slices
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
