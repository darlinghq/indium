#include <iridium/iridium.hpp>
#include <iridium/air.hpp>
#include <iridium/spirv.hpp>
#include <iridium/dynamic-llvm.hpp>

#include <llvm-c/Core.h>

#include <cstdlib>

bool Iridium::init() {
	return Iridium::DynamicLLVM::init();
};

void Iridium::finit() {
	Iridium::DynamicLLVM::finit();
};

void* Iridium::translate(const void* inputData, size_t inputSize, size_t& outputSize, OutputInfo& outputInfo) {
	AIR::Library lib(inputData, inputSize);
	SPIRV::Builder builder;

	lib.buildModule(builder, outputInfo);

	auto result = builder.finalize(outputSize);

	return result;
};
