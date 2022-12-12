project(indium)
cmake_minimum_required(VERSION 3.20)

find_package(Vulkan REQUIRED)
find_package(LLVM REQUIRED CONFIG)

if (DARLING)
	set(INDIUM_TARGETS indium indium_private iridium)
else()
	set(INDIUM_TARGETS indium indium_kit indium_private indium_kit_private iridium mtl2spv)
endif()

set(indium_sources
	src/indium/blit-command-encoder.cpp
	src/indium/buffer.cpp
	src/indium/command-buffer.cpp
	src/indium/command-encoder.cpp
	src/indium/command-queue.cpp
	src/indium/compute-command-encoder.cpp
	src/indium/compute-pipeline.cpp
	src/indium/counter.cpp
	src/indium/depth-stencil.cpp
	src/indium/device.cpp
	src/indium/drawable.cpp
	src/indium/indium.cpp
	src/indium/library.cpp
	src/indium/render-command-encoder.cpp
	src/indium/render-pipeline.cpp
	src/indium/resource.cpp
	src/indium/sampler.cpp
	src/indium/texture.cpp
)

set(iridium_sources
	src/iridium/air.cpp
	src/iridium/iridium.cpp
	src/iridium/spirv.cpp
)

if (DARLING)
	set(DYLIB_INSTALL_NAME "/usr/lib/darling/libindium.dylib")
	add_darling_library(indium ${indium_sources})
	make_fat(indium)
else()
	add_library(indium SHARED ${indium_sources})
endif()

add_library(indium_private INTERFACE)

if (NOT DARLING)
	add_library(indium_kit SHARED
		src/indium-kit/layer.cpp
	)

	add_library(indium_kit_private INTERFACE)
endif()

if (DARLING)
	set(DYLIB_INSTALL_NAME "/usr/lib/darling/libiridium.dylib")
	add_darling_library(iridium ${iridium_sources})
	make_fat(iridium)
else()
	add_library(iridium SHARED ${iridium_sources})
endif()

if (NOT DARLING)
	add_executable(mtl2spv
		src/mtl2spv/mtl2spv.cpp
	)
endif()

set_target_properties(${INDIUM_TARGETS}
	PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
)

target_include_directories(indium PUBLIC
	include
)

if (NOT DARLING)
	target_include_directories(indium_kit PUBLIC
		${Vulkan_INCLUDE_DIRS}
		include
	)
endif()

target_include_directories(iridium PUBLIC
	include
)

target_include_directories(indium_private INTERFACE
	${Vulkan_INCLUDE_DIRS}
	private-include
)

if (NOT DARLING)
	target_include_directories(indium_kit_private INTERFACE
		private-include
	)
endif()

target_include_directories(indium PRIVATE
	${Vulkan_INCLUDE_DIRS}
	private-include
)

if (NOT DARLING)
	target_include_directories(indium_kit PRIVATE
		private-include
	)
endif()

target_include_directories(iridium PRIVATE
	private-include
	${LLVM_INCLUDE_DIRS}
)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(iridium PRIVATE ${LLVM_DEFINITIONS_LIST})

if (DARLING)
	target_link_libraries(indium PRIVATE
		Vulkan
		iridium
		cxx
		system
	)

	target_link_libraries(indium_private INTERFACE
		Vulkan
		indium
	)
else()
	target_link_libraries(indium PRIVATE
		${Vulkan_LIBRARIES}
		iridium
	)

	# if you're linking to the private interface, you're going to want to link to
	# Vulkan so that you can implement things like Textures and Drawables
	target_link_libraries(indium_private INTERFACE
		${Vulkan_LIBRARIES}
		indium
	)
endif()

if (NOT DARLING)
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
endif()

if (DARLING)
	target_link_libraries(iridium PRIVATE
		host_LLVM
		cxx
		system
	)
else()
	llvm_config(iridium USE_SHARED support core bitreader)
endif()

if (NOT DARLING)
	target_link_libraries(mtl2spv PRIVATE
		iridium
	)
endif()

if (DARLING)
	install(TARGETS indium iridium DESTINATION libexec/darling/usr/lib/darling)
else()
	install(TARGETS indium indium_kit iridium LIBRARY)
	install(TARGETS mtl2spv RUNTIME)
	install(DIRECTORY include/ DESTINATION include)

	option(ENABLE_TESTS "Whether or not to enable tests" OFF)

	if (ENABLE_TESTS)
		add_subdirectory(test)
	endif ()
endif()
