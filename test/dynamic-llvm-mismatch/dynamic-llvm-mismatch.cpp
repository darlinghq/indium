#include <iridium/dynamic-llvm.hpp>

#include <cstdio>
#include <exception>

// DynamicLLVM::init() runs from Metal's library constructor, so on a major-version mismatch it
// must report on stderr and return false instead of throwing (an exception escaping a constructor
// terminates every process that loads Metal). HOST_LLVM_LIBNAME names fake-llvm.cpp's library,
// which reports LLVM 1.
int main() {
	try {
		if (Iridium::DynamicLLVM::init()) {
			fprintf(stderr, "FAIL: init() accepted a libLLVM of a different major\n");
			return 1;
		}
	} catch (const std::exception& e) {
		fprintf(stderr, "FAIL: init() threw: %s\n", e.what());
		return 1;
	}
	printf("PASS\n");
	return 0;
}
