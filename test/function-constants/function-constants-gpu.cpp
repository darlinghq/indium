// Copyright (C) 2026 Lubos Dolezel
//
// A function made with function constant values runs the shader with those values; the same library
// gives different results for different values; a constant that was not given a value is undefined;
// values that do not fit the function are refused. Needs a Vulkan device (prints SKIP without one).

#include "function-constants.h"

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

uint32_t bits(float value) {
	uint32_t result;
	std::memcpy(&result, &value, sizeof(result));
	return result;
}

constexpr size_t count = 8;

// Runs the kernel over `count` elements; `extra` may be null.
std::vector<float> run(const std::shared_ptr<Indium::Device>& device, const std::shared_ptr<Indium::Function>& function, const float* extra) {
	auto pipeline = device->newComputePipelineState(function);
	auto queue = device->newCommandQueue();
	auto out = device->newBuffer(count * sizeof(float), Indium::ResourceOptions::StorageModeShared);
	std::memset(out->contents(), 0, count * sizeof(float));
	std::shared_ptr<Indium::Buffer> extraBuffer;
	if (extra) {
		extraBuffer = device->newBuffer(extra, count * sizeof(float), Indium::ResourceOptions::StorageModeShared);
	}

	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(out, 0, 0);
	if (extraBuffer) {
		encoder->setBuffer(extraBuffer, 0, 1);
	}
	encoder->dispatchThreads(Indium::Size { count, 1, 1 }, Indium::Size { count, 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	auto data = static_cast<const float*>(out->contents());
	return std::vector<float>(data, data + count);
}

bool all(const std::vector<float>& values, float expected) {
	for (auto value: values) {
		if (value != expected) {
			return false;
		}
	}
	return true;
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

		auto library = device->newLibrary(function_constants_metallib, function_constants_metallib_len);

		{
			auto function = library->newFunction("fc_simple");
			check(function->functionConstants().size() == 2, "fc_simple declares the two constants it uses");
		}

		{
			Indium::FunctionConstantValues values;
			values.setValue(1, Indium::FunctionConstantType::Int, 3);
			values.setValue(2, Indium::FunctionConstantType::Float, bits(2.5f));
			auto function = library->newFunction("fc_simple", values);
			check(function != nullptr && all(run(device, function, nullptr), 7.5f), "mode 3, factor 2.5 gives 7.5");
		}
		{
			Indium::FunctionConstantValues values;
			values.setValue("mode", Indium::FunctionConstantType::Int, static_cast<uint32_t>(-4));
			values.setValue("factor", Indium::FunctionConstantType::Float, bits(0.5f));
			auto function = library->newFunction("fc_simple", values);
			check(function != nullptr && all(run(device, function, nullptr), -2.0f), "the same library, by name: mode -4, factor 0.5 gives -2");
		}

		std::vector<float> extra { 0, 1, 2, 3, 4, 5, 6, 7 };
		{
			Indium::FunctionConstantValues values;
			values.setValue(0, Indium::FunctionConstantType::Bool, 1);
			values.setValue(1, Indium::FunctionConstantType::Int, 2);
			values.setValue(2, Indium::FunctionConstantType::Float, bits(3.0f));
			values.setValue(3, Indium::FunctionConstantType::UInt, 10);
			auto function = library->newFunction("fc_kernel", values);
			auto result = function ? run(device, function, extra.data()) : std::vector<float>();
			bool ok = result.size() == count;
			for (size_t i = 0; ok && i < count; ++i) {
				ok = result[i] == 2 * 3.0f + 10 + extra[i];
			}
			check(ok, "use_scale, opt defined and the extra buffer all take part");
		}
		{
			Indium::FunctionConstantValues values;
			values.setValue(0, Indium::FunctionConstantType::Bool, 0);
			values.setValue(1, Indium::FunctionConstantType::Int, 4);
			values.setValue(2, Indium::FunctionConstantType::Float, bits(100.0f));
			auto function = library->newFunction("fc_kernel", values);
			check(function != nullptr && all(run(device, function, nullptr), 4.0f), "use_scale false ignores factor and the extra buffer, and an undefined opt adds nothing");
		}

		{
			std::string message;
			Indium::FunctionConstantValues wrongType;
			wrongType.setValue(1, Indium::FunctionConstantType::Float, bits(1.0f));
			check(library->newFunction("fc_simple", wrongType, &message) == nullptr && !message.empty(), "a value of another type than the constant is refused");
			// values for constants a function does not declare are ignored: this one is shared
			Indium::FunctionConstantValues shared;
			shared.setValue(1, Indium::FunctionConstantType::Int, 2);
			shared.setValue(2, Indium::FunctionConstantType::Float, bits(4.0f));
			shared.setValue(9, Indium::FunctionConstantType::Int, 1);
			shared.setValue("nope", Indium::FunctionConstantType::Float, 1);
			auto function = library->newFunction("fc_simple", shared, &message);
			check(function != nullptr && all(run(device, function, nullptr), 8.0f), "values for constants the function does not have are ignored");
			check(library->newFunction("no_such_function", Indium::FunctionConstantValues {}) == nullptr, "an unknown function is null");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
