#include <iridium/dynamic-llvm.hpp>
#include <llvm/Config/llvm-config.h>
#include <cstdio>

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

	if (!libraryHandle) return false;
	// LLVM's C API enum values changed in LLVM 23. A different runtime major
	// misclassifies constants and branch instructions even when symbols resolve.
	using GetVersion = void (*)(unsigned*, unsigned*, unsigned*);
	auto getVersion = reinterpret_cast<GetVersion>(resolveSymbol("LLVMGetVersion"));
	unsigned major = 0, minor = 0, patch = 0;
	if (getVersion) getVersion(&major, &minor, &patch);
	if (major != LLVM_VERSION_MAJOR) {
		finit();
		// init() runs from Metal's library constructor, where an escaping exception would terminate every
		// process that loads Metal, so report the mismatch and fail like any other load failure.
		char loaded[64];
		if (getVersion) snprintf(loaded, sizeof loaded, "LLVM %u", major);
		else snprintf(loaded, sizeof loaded, "an LLVM without LLVMGetVersion (older than 16)");
		fprintf(stderr, "Iridium LLVM version mismatch: built with LLVM %d, loaded %s from " HOST_LLVM_LIBNAME
			"; rebuild and reinstall the runtime with matching LLVM headers and library\n", LLVM_VERSION_MAJOR, loaded);
		return false;
	}
	return true;
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
