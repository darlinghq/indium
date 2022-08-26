project(indium)
cmake_minimum_required(VERSION 3.20)

find_package(Vulkan REQUIRED)

add_library(indium SHARED
	src/indium/buffer.cpp
	src/indium/command-buffer.cpp
	src/indium/command-encoder.cpp
	src/indium/command-queue.cpp
	src/indium/device.cpp
	src/indium/drawable.cpp
	src/indium/indium.cpp
	src/indium/library.cpp
	src/indium/render-command-encoder.cpp
	src/indium/render-pipeline.cpp
	src/indium/resource.cpp
	src/indium/texture.cpp
)

add_library(indium_private INTERFACE)

add_library(indium_kit SHARED
	src/indium-kit/layer.cpp
)

add_library(indium_kit_private INTERFACE)

set_target_properties(indium indium_kit indium_private indium_kit_private
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)

target_include_directories(indium PUBLIC
	include
)

target_include_directories(indium_kit PUBLIC
	${Vulkan_INCLUDE_DIRS}
	include
)

target_include_directories(indium_private INTERFACE
	${Vulkan_INCLUDE_DIRS}
	private-include
)

target_include_directories(indium_kit_private INTERFACE
	private-include
)

target_include_directories(indium PRIVATE
	${Vulkan_INCLUDE_DIRS}
	private-include
)

target_include_directories(indium_kit PRIVATE
	private-include
)

target_link_libraries(indium PRIVATE
	${Vulkan_LIBRARIES}
)

# if you're linking to the private interface, you're going to want to link to
# Vulkan so that you can implement things like Textures and Drawables
target_link_libraries(indium_private INTERFACE
	${Vulkan_LIBRARIES}
	indium
)

# IndiumKit's API requires the caller to provide Vulkan objects in certain places
target_link_libraries(indium_kit PUBLIC
	${Vulkan_LIBRARIES}
	indium
)

target_link_libraries(indium_kit PRIVATE
	indium_private
)

target_link_libraries(indium_kit_private INTERFACE
	indium_kit
	indium_private
)

install(TARGETS indium indium_kit LIBRARY)
install(DIRECTORY include/ DESTINATION include)

add_subdirectory(test)
