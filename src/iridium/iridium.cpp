#include <iridium/iridium.hpp>
#include <iridium/air.hpp>
#include <iridium/spirv.hpp>
#include <iridium/dynamic-llvm.hpp>

#include <llvm-c/Core.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

bool Iridium::init() {
	return Iridium::DynamicLLVM::init();
};

void Iridium::finit() {
	Iridium::DynamicLLVM::finit();
};

void* Iridium::translate(const void* inputData, size_t inputSize, size_t& outputSize, OutputInfo& outputInfo) {
	// translate() is documented to return nullptr on failure. AIR analysis
	// signals unsupported input by throwing, which would otherwise abort the
	// caller (and, inside a Metal app, take down the whole process).
	try {
		AIR::Library lib(inputData, inputSize);
		SPIRV::Builder builder;

		lib.buildModule(builder, outputInfo);

		return builder.finalize(outputSize);
	} catch (const std::exception& e) {
		fprintf(stderr, "Iridium: translation failed: %s\n", e.what());
		outputSize = 0;
		return nullptr;
	}
};
