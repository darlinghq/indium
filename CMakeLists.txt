project(indium)
cmake_minimum_required(VERSION 3.20)

find_package(Vulkan REQUIRED)

# Iridium compiles against these LLVM headers and loads the shared libLLVM of the same major at run time, so the LLVM
# found here needs an llvm-config that reports a shared library. find_package also looks in <prefix>/llvm/lib/cmake/llvm
# for every PATH entry, so with /opt/rocm/bin on PATH it picks ROCm's private LLVM ahead of the system one, and a cached
# LLVM_DIR can name an LLVM that was removed since. Unless LLVM_DIR names a usable LLVM, use the llvm-config on PATH.
function(indium_llvm_config_works tool result)
	set(${result} FALSE PARENT_SCOPE)
	execute_process(COMMAND "${tool}" --link-shared --libnames
		OUTPUT_VARIABLE libname RESULT_VARIABLE status ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
	if (status EQUAL 0 AND libname)
		set(${result} TRUE PARENT_SCOPE)
	endif()
endfunction()

set(indium_llvm_usable FALSE)
if (LLVM_DIR)
	# <prefix>/lib/cmake/llvm or <prefix>/lib64/cmake/llvm
	get_filename_component(indium_llvm_prefix "${LLVM_DIR}/../../.." ABSOLUTE)
	indium_llvm_config_works("${indium_llvm_prefix}/bin/llvm-config" indium_llvm_usable)
endif()
if (NOT indium_llvm_usable)
	find_program(INDIUM_PATH_LLVM_CONFIG llvm-config)
	if (INDIUM_PATH_LLVM_CONFIG)
		indium_llvm_config_works("${INDIUM_PATH_LLVM_CONFIG}" indium_path_llvm_usable)
		execute_process(COMMAND "${INDIUM_PATH_LLVM_CONFIG}" --cmakedir
			OUTPUT_VARIABLE indium_path_llvm_dir RESULT_VARIABLE indium_status ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
		if (indium_path_llvm_usable AND indium_status EQUAL 0 AND EXISTS "${indium_path_llvm_dir}")
			if (LLVM_DIR)
				message(STATUS "LLVM_DIR=${LLVM_DIR} has no llvm-config with a shared library; using ${indium_path_llvm_dir}")
			endif()
			set(LLVM_DIR "${indium_path_llvm_dir}" CACHE PATH "The directory containing a CMake configuration file for LLVM." FORCE)
		endif()
	endif()
endif()

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
	src/indium/dynamic-vk.cpp
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
	src/iridium/dynamic-llvm.cpp
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
		iridium
		cxx
		system
	)

	target_link_libraries(indium_private INTERFACE
		indium
	)
else()
	target_link_libraries(indium PRIVATE
		iridium
	)

	target_link_libraries(indium_private INTERFACE
		indium
	)
endif()

if (NOT DARLING)
	target_link_libraries(indium_kit PUBLIC
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

# Use an llvm-config of the same major as the headers found above. A stale cached LLVM_DIR can point at
# a removed LLVM whose llvm-config exists but whose library is gone, so fall back to the one on PATH.
set(HOST_LLVM_LIBNAME "")
foreach(candidate IN ITEMS "${LLVM_TOOLS_BINARY_DIR}/llvm-config" llvm-config)
	execute_process(
		COMMAND "${candidate}" --version
		OUTPUT_VARIABLE candidate_version
		RESULT_VARIABLE candidate_status
		ERROR_QUIET
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if (candidate_status EQUAL 0 AND candidate_version MATCHES "^${LLVM_VERSION_MAJOR}\\.")
		execute_process(
			COMMAND "${candidate}" --link-shared --libnames
			OUTPUT_VARIABLE HOST_LLVM_LIBNAME
			RESULT_VARIABLE candidate_status
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
		if (candidate_status EQUAL 0 AND HOST_LLVM_LIBNAME)
			break()
		endif()
		set(HOST_LLVM_LIBNAME "")
	endif()
endforeach()
if (NOT HOST_LLVM_LIBNAME)
	message(FATAL_ERROR "No working llvm-config for LLVM ${LLVM_VERSION} (LLVM_DIR=${LLVM_DIR}). "
		"Install an LLVM with a shared libLLVM, or pass -DLLVM_DIR=<its lib/cmake/llvm directory>.")
endif()

target_compile_definitions(iridium PRIVATE
	"HOST_LLVM_LIBNAME=\"${HOST_LLVM_LIBNAME}\""
)

if (DARLING)
	target_link_libraries(iridium PRIVATE
		cxx
		system
	)
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
		enable_testing()
		add_subdirectory(test)
	endif ()
endif()
