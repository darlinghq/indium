#include <iridium/dynamic-llvm.hpp>

#ifdef DARLING
	#include <elfcalls.h>
	extern "C" struct elf_calls* _elfcalls;
#endif

void* Iridium::DynamicLLVM::libraryHandle = NULL;

#define DYNAMICLLVM_FUNCTION_DEF(_name) \
	Iridium::DynamicLLVM::DynamicFunction<decltype(_name)> Iridium::DynamicLLVM::_name(#_name);

IRIDIUM_DYNAMICLLVM_FUNCTION_FOREACH(DYNAMICLLVM_FUNCTION_DEF)

bool Iridium::DynamicLLVM::init() {
#ifdef DARLING
	// like we do in Indium, we have to check if the host system has LLVM
	// before loading our LLVM wrapper library since our wrapper will die
	// if it fails to load the host library
	void* tmp = _elfcalls->dlopen(HOST_LLVM_LIBNAME);
	if (!tmp) {
		// host has no (compatible) LLVM library
		return false;
	}
	// keep it open to avoid having to reopen it when we load the wrapper

	libraryHandle = dlopen("/usr/lib/native/libhost_LLVM.dylib", RTLD_LAZY);

	// now we can close the native handle
	_elfcalls->dlclose(tmp);
#else
	libraryHandle = dlopen(HOST_LLVM_LIBNAME, RTLD_LAZY);
#endif

	return !!libraryHandle;
};

void Iridium::DynamicLLVM::finit() {
	if (libraryHandle) {
		dlclose(libraryHandle);
		libraryHandle = NULL;
	}
};
