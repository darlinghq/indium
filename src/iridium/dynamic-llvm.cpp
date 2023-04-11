#include <iridium/dynamic-llvm.hpp>

#ifdef DARLING
	#include <elfcalls.h>
	extern "C" struct elf_calls* _elfcalls;
#endif

static void* libraryHandle = NULL;

#define DYNAMICLLVM_FUNCTION_DEF(_name) \
	Iridium::DynamicLLVM::DynamicFunction<decltype(_name)> Iridium::DynamicLLVM::_name(#_name);

IRIDIUM_DYNAMICLLVM_FUNCTION_FOREACH(DYNAMICLLVM_FUNCTION_DEF)

bool Iridium::DynamicLLVM::init() {
#ifdef DARLING
	libraryHandle = _elfcalls->dlopen(HOST_LLVM_LIBNAME);
#else
	libraryHandle = dlopen(HOST_LLVM_LIBNAME, RTLD_LAZY);
#endif

	return !!libraryHandle;
};

void Iridium::DynamicLLVM::finit() {
	if (libraryHandle) {
#ifdef DARLING
		_elfcalls->dlclose(libraryHandle);
#else
		dlclose(libraryHandle);
#endif
		libraryHandle = NULL;
	}
};

void* Iridium::DynamicLLVM::resolveSymbol(const char* name) {
#ifdef DARLING
	return _elfcalls->dlsym(libraryHandle, name);
#else
	return dlsym(libraryHandle, name);
#endif
};
