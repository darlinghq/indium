project(indium-test-swapchain-surface)

find_package(X11)

if (X11_FOUND)
	add_executable(indium-test-swapchain-surface swapchain-surface.cpp)

	target_include_directories(indium-test-swapchain-surface PRIVATE ${X11_INCLUDE_DIR})

	target_link_libraries(indium-test-swapchain-surface PRIVATE
		${X11_LIBRARIES}
		indium_kit
		indium_private
	)

	set_target_properties(indium-test-swapchain-surface
		PROPERTIES
			CXX_STANDARD 17
			CXX_STANDARD_REQUIRED ON
			CXX_EXTENSIONS OFF
	)
endif()
