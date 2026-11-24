// Copyright (C) 2026 Darling Developers
//
// Function constants of every scalar type, in a library made from SPIR-V: a value given to a function reaches
// its pipelines at the constant's own size, whatever the size of the values around it, and the defined flags
// say which constants were given one. Needs a Vulkan device (prints SKIP without one).

#include "function-constant-types-spv.h"

#include <indium/indium.hpp>

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	failures += !ok;
}

using Type = Indium::FunctionConstantType;

// the constants of function-constant-types.metal, in the order of their indices
const struct { const char* name; Type type; } constants[] = {
	{ "cb", Type::Bool }, { "ci", Type::Int }, { "cu", Type::UInt }, { "cf", Type::Float },
	{ "cc", Type::Char }, { "cuc", Type::UChar }, { "cs", Type::Short }, { "cus", Type::UShort },
	{ "ch", Type::Half }, { "cl", Type::Long }, { "cul", Type::ULong },
};
constexpr size_t constantCount = sizeof(constants) / sizeof(constants[0]);

// out[0] to out[10] hold the constants as the kernel reads them, out[11] a bit per defined constant
constexpr size_t outputCount = constantCount + 1;

Indium::LibraryReflection reflection() {
	Indium::LibraryReflection result;
	auto& function = result.functions["types"];
	function.functionType = Indium::FunctionType::Kernel;
	function.bindings.push_back(Indium::BindingDescriptor { 0, 0, SIZE_MAX, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
	for (size_t i = 0; i < constantCount; ++i) {
		Indium::FunctionConstantInfo constant;
		constant.name = constants[i].name;
		constant.type = constants[i].type;
		constant.index = i;
		constant.specializationID = 65536 + i;
		constant.definedSpecializationID = 131072 + i;
		function.functionConstants.push_back(constant);
	}
	return result;
}

uint32_t floatBits(float value) {
	uint32_t result;
	std::memcpy(&result, &value, sizeof(result));
	return result;
}

std::vector<uint64_t> run(const std::shared_ptr<Indium::Device>& device, const std::shared_ptr<Indium::Function>& function) {
	auto pipeline = device->newComputePipelineState(function);
	auto queue = device->newCommandQueue();
	auto out = device->newBuffer(outputCount * sizeof(uint64_t), Indium::ResourceOptions::StorageModeShared);
	std::memset(out->contents(), 0xab, outputCount * sizeof(uint64_t));

	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(out, 0, 0);
	encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { 1, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	auto data = static_cast<const uint64_t*>(out->contents());
	return std::vector<uint64_t>(data, data + outputCount);
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		std::string error;
		auto library = device->newLibrary(function_constant_types_spv, function_constant_types_spv_len, reflection(), &error);
		check(library != nullptr, "the library is made");
		if (!library) {
			std::printf("newLibrary: %s\n", error.c_str());
		} else {
			Indium::FunctionConstantValues values;
			values.setValue(0, Type::Bool, 1);
			values.setValue(1, Type::Int, static_cast<uint32_t>(-5));
			values.setValue(2, Type::UInt, 0x80000001u);
			values.setValue(3, Type::Float, floatBits(2.5f));
			values.setValue(4, Type::Char, static_cast<uint8_t>(-3));
			values.setValue(5, Type::UChar, 200);
			values.setValue(6, Type::Short, static_cast<uint16_t>(-7));
			values.setValue(7, Type::UShort, 65000);
			values.setValue(8, Type::Half, 0x4100); // 2.5
			values.setValue(9, Type::Long, static_cast<uint64_t>(-123456789012345));
			values.setValue("cul", Type::ULong, 0x1122334455667788);
			auto function = library->newFunction("types", values, &error);
			auto out = function ? run(device, function) : std::vector<uint64_t>(outputCount, 0);
			check(out[0] == 1, "bool");
			check(out[1] == static_cast<uint64_t>(-5), "int");
			check(out[2] == 0x80000001u, "uint");
			check(out[3] == 10, "float");
			check(out[4] == static_cast<uint64_t>(-3), "char");
			check(out[5] == 200, "uchar");
			check(out[6] == static_cast<uint64_t>(-7), "short");
			check(out[7] == 65000, "ushort");
			check(out[8] == 10, "half");
			check(out[9] == static_cast<uint64_t>(-123456789012345), "long");
			check(out[10] == 0x1122334455667788, "ulong");
			check(out[11] == 0x7ff, "every constant given a value is defined");

			// the same low 32 bits, other high bits
			Indium::FunctionConstantValues high;
			high.setValue(10, Type::ULong, 0xffeeddcc55667788);
			high.setValue(9, Type::Long, 0x0000000155667788);
			function = library->newFunction("types", high, &error);
			out = function ? run(device, function) : std::vector<uint64_t>(outputCount, 0);
			check(out[10] == 0xffeeddcc55667788 && out[9] == 0x0000000155667788, "the high 32 bits of a long and a ulong");
			check(out[11] == 0x600, "only the constants given a value are defined");

			function = library->newFunction("types", Indium::FunctionConstantValues {}, &error);
			out = function ? run(device, function) : std::vector<uint64_t>(outputCount, 1);
			bool zero = true;
			for (size_t i = 0; i < outputCount; ++i) {
				zero = zero && out[i] == 0;
			}
			check(zero, "without values, every constant is zero and none is defined");

			Indium::FunctionConstantValues wrongType;
			wrongType.setValue(10, Type::Long, 1);
			error.clear();
			check(library->newFunction("types", wrongType, &error) == nullptr && !error.empty(), "a long value for a ulong constant is refused");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
