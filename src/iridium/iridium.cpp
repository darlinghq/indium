#include <iridium/iridium.hpp>
#include <iridium/air.hpp>
#include <iridium/spirv.hpp>

#include <llvm-c/Core.h>

#include <cstdlib>

void Iridium::init() {
};

void Iridium::finit() {
};

void* Iridium::translate(const void* inputData, size_t inputSize, size_t& outputSize, OutputInfo& outputInfo) {
	AIR::Library lib(inputData, inputSize);
	SPIRV::Builder builder;

	lib.buildModule(builder, outputInfo);

	auto result = builder.finalize(outputSize);

	return result;
};
