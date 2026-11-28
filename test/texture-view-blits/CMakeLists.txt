project(indium-test-texture-view-blits)

add_executable(indium-test-texture-view-blits texture-view-blits.cpp)

target_link_libraries(indium-test-texture-view-blits PRIVATE
	indium
	indium_private
)

set_target_properties(indium-test-texture-view-blits
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)
