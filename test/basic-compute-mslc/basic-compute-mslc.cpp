/*
A test for a library built from SPIR-V that mslc produced, loaded through
Device::newLibrary(const void*, size_t, const LibraryReflection&).

The shader is the one test/basic-compute uses, and the bar is the one
test/basic-compute sets: 16.7M elements, dispatched on the real device, compared
bit for bit against what the Metal source says the result has to be.

Two things make this more than basic-compute with different bytes.

The reflection is read out of mslc's JSON rather than written out by hand, so
the test says something about mslc's output. Reading it here stands in for what
darling-metal does with NSJSONSerialization: indium takes the values and has no
opinion about the document they came from, so the part that knows mslc's format
belongs in the layer that would do the same job for real. It reads the keys
indium needs and stops on anything else, rather than tolerating a change it
would then silently ignore.

The module bytes and the reflection are released as soon as newLibrary returns,
before anything is dispatched, because the contract is that indium borrows both
for the duration of the call. A library that had quietly kept a pointer would go
wrong here rather than at some later point.
*/

#include "add-mslc-reflect.h"
#include "add-mslc-spv.h"

#include <indium/indium.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t arrayLength = 1 << 24;
static constexpr size_t bufferSize = arrayLength * sizeof(float);

// Written into the result buffer before each dispatch, so an element the shader
// never wrote is a mismatch rather than a coincidence.
static constexpr float poison = -1234.5f;

static constexpr size_t noIndex = std::numeric_limits<size_t>::max();

// --- the stand-in for the Objective-C layer's JSON step ---

static bool reflectionFailed(const std::string& message) {
	std::cerr << "Reflection ERROR: " << message << std::endl;
	return false;
}

static std::string readString(const std::string& document, const std::string& key) {
	auto at = document.find("\"" + key + "\"");

	if (at == std::string::npos) {
		return "";
	}

	auto colon = document.find(':', at);
	auto open = document.find('"', colon);
	auto close = document.find('"', open + 1);

	if (colon == std::string::npos || open == std::string::npos || close == std::string::npos) {
		return "";
	}

	return document.substr(open + 1, close - open - 1);
}

// Reads the number that follows the key the caller found, so the caller's
// position is the key rather than the value.
static bool readNumber(const std::string& text, size_t from, size_t& value) {
	auto colon = text.find(':', from);

	if (colon == std::string::npos) {
		return false;
	}

	from = colon + 1;

	while (from < text.size() && (text[from] == ' ' || text[from] == ',')) {
		++from;
	}

	if (from >= text.size() || text[from] < '0' || text[from] > '9') {
		return false;
	}

	value = std::strtoull(text.c_str() + from, nullptr, 10);

	return true;
}

static bool readFunctionType(const std::string& stage, Indium::FunctionType& functionType) {
	if (stage == "kernel") {
		functionType = Indium::FunctionType::Kernel;
	} else if (stage == "vertex") {
		functionType = Indium::FunctionType::Vertex;
	} else if (stage == "fragment") {
		functionType = Indium::FunctionType::Fragment;
	} else {
		return false;
	}

	return true;
}

// Walks the bindings array one object at a time, so a binding's own keys are
// looked for inside that object rather than anywhere in the document.
static bool readBindings(const std::string& document, Indium::FunctionReflection& functionReflection) {
	auto array = document.find("\"bindings\"");

	if (array == std::string::npos) {
		return reflectionFailed("no bindings array");
	}

	array = document.find('[', array);

	if (array == std::string::npos) {
		return reflectionFailed("bindings is not an array");
	}

	while (true) {
		auto open = document.find('{', array);
		auto close = document.find('}', open);

		if (open == std::string::npos || close == std::string::npos) {
			break;
		}

		std::string object = document.substr(open, close - open);

		array = close + 1;

		Indium::BindingDescriptor binding;

		std::string kind = readString(object, "kind");

		if (kind == "Buffer") {
			binding.type = Indium::BindingType::Buffer;
		} else if (kind == "Texture") {
			binding.type = Indium::BindingType::Texture;
		} else if (kind == "Sampler") {
			binding.type = Indium::BindingType::Sampler;
		} else {
			// An unrecognised kind is a reason to stop, not a reason to guess:
			// guessing is how a texture ends up bound as a buffer.
			return reflectionFailed("binding of unknown kind '" + kind + "'");
		}

		size_t value = 0;

		auto metalIndex = object.find("\"metal_index\"");

		if (metalIndex == std::string::npos || !readNumber(object, metalIndex, value)) {
			return reflectionFailed("a binding has no metal_index");
		}

		binding.index = value;

		auto descriptor = object.find("\"descriptor\"");

		if (descriptor == std::string::npos) {
			return reflectionFailed("a binding has no descriptor");
		}

		auto bindingNumber = object.find("\"binding\"", descriptor);

		if (bindingNumber == std::string::npos || !readNumber(object, bindingNumber, value)) {
			return reflectionFailed("a binding has no descriptor binding number");
		}

		binding.internalIndex = value;

		functionReflection.bindings.push_back(binding);
	}

	return true;
}

static bool readReflection(const std::string& document, Indium::LibraryReflection& reflection) {
	Indium::FunctionReflection functionReflection;

	std::string stage = readString(document, "stage");

	if (!readFunctionType(stage, functionReflection.functionType)) {
		return reflectionFailed("unknown stage '" + stage + "'");
	}

	if (!readBindings(document, functionReflection)) {
		return false;
	}

	std::string name = readString(document, "entry_point");

	if (name.empty()) {
		return reflectionFailed("no entry point");
	}

	reflection.functions.emplace(std::move(name), std::move(functionReflection));

	return true;
}

// --- the comparison, in one place, so the control below uses the same one ---

static size_t mismatches(const float* result, const float* a, const float* b, size_t count) {
	size_t bad = 0;

	for (size_t i = 0; i < count; ++i) {
		// Bit for bit rather than a tolerance: the shader adds the same two
		// floats the host adds, so every element has to come back equal, and a
		// comparison that passed on a NaN would be passing on nothing.
		float expected = a[i] + b[i];

		if (std::isnan(expected) || std::memcmp(&result[i], &expected, sizeof(float)) != 0) {
			++bad;
		}
	}

	return bad;
}

int main(int argc, char** argv) {
	Indium::init(nullptr, 0, ENABLE_VALIDATION);

	bool ok = true;

	{
		auto device = Indium::createSystemDefaultDevice();

		std::cout << "device: " << device->name() << std::endl;

		bool keepPollingDevice = true;

		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		Indium::LibraryReflection reflection;

		if (!readReflection(std::string(reinterpret_cast<const char*>(add_mslc_reflect), add_mslc_reflect_len), reflection)) {
			ok = false;
		}

		// The module goes into a buffer this test owns, so it can be released
		// straight after the call. The embedded fixture stays where it is, for
		// the rejection checks further down.
		std::vector<unsigned char> module(add_mslc_spv, add_mslc_spv + add_mslc_spv_len);

		std::string error;
		auto lib = device->newLibrary(module.data(), module.size(), reflection, &error);

		if (!lib) {
			std::cerr << "newLibrary ERROR: " << error << std::endl;
			ok = false;
		}

		// The module and the reflection are this test's again from here on, and
		// nothing below reads either of them: a library that had kept a pointer
		// to either would go wrong from here on.
		std::vector<unsigned char>().swap(module);
		reflection = Indium::LibraryReflection();

		if (lib) {
			auto func = lib->newFunction("add_arrays");

			if (!func) {
				std::cerr << "newFunction ERROR: the reflection names a function the library does not have" << std::endl;
				ok = false;
			} else {
				auto pso = device->newComputePipelineState(func);
				auto commandQueue = device->newCommandQueue();

				auto bufA = device->newBuffer(bufferSize, Indium::ResourceOptions::StorageModeShared);
				auto bufB = device->newBuffer(bufferSize, Indium::ResourceOptions::StorageModeShared);
				auto bufResult = device->newBuffer(bufferSize, Indium::ResourceOptions::StorageModeShared);

				auto a = static_cast<float*>(bufA->contents());
				auto b = static_cast<float*>(bufB->contents());
				auto result = static_cast<float*>(bufResult->contents());

				for (size_t i = 0; i < arrayLength; ++i) {
					a[i] = (float)rand() / (float)RAND_MAX;
					b[i] = (float)rand() / (float)RAND_MAX;
					result[i] = poison;
				}

				size_t threadGroupSize = pso->maxTotalThreadsPerThreadgroup();
				if (threadGroupSize > arrayLength) {
					threadGroupSize = arrayLength;
				}

				auto cmdbuf = commandQueue->commandBuffer();
				auto encoder = cmdbuf->computeCommandEncoder();

				encoder->setComputePipelineState(pso);
				encoder->setBuffer(bufA, 0, 0);
				encoder->setBuffer(bufB, 0, 1);
				encoder->setBuffer(bufResult, 0, 2);
				encoder->dispatchThreads(Indium::Size { arrayLength, 1, 1 }, Indium::Size { threadGroupSize, 1, 1 });

				encoder->endEncoding();
				cmdbuf->commit();
				cmdbuf->waitUntilCompleted();

				auto bad = mismatches(result, a, b, arrayLength);

				if (bad == 0) {
					std::cout << "mslc's SPIR-V for add.metal computes a+b over " << arrayLength << " elements, bit exact" << std::endl;
				} else {
					std::cerr << "Readback ERROR: " << bad << " of " << arrayLength << " elements differ from a+b" << std::endl;
					ok = false;
				}

				// Negative control 1: the comparison has to be able to fail. One
				// wrong element has to be counted as one wrong element, and has
				// to stop being counted once it is put back.
				float saved = result[arrayLength - 1];
				result[arrayLength - 1] = poison;

				auto perturbed = mismatches(result, a, b, arrayLength);

				result[arrayLength - 1] = saved;

				auto restored = mismatches(result, a, b, arrayLength);

				std::cout << "Negative control: " << perturbed << " mismatch(es) with one element wrong, "
					<< restored << " with it put back" << std::endl;

				if (perturbed != 1 || restored != 0) {
					std::cerr << "Control ERROR: the comparison does not notice exactly one wrong element" << std::endl;
					ok = false;
				}

				// Negative control 2: the metal_index values out of mslc's
				// reflection are what route the buffers. The same module
				// dispatched through a reflection whose buffer indices are
				// rotated has to write somewhere else, which is what makes the
				// pass above evidence about the reflection rather than about
				// addition.
				Indium::LibraryReflection rotated;
				auto& rotatedFunction = rotated.functions["add_arrays"];
				rotatedFunction.functionType = Indium::FunctionType::Kernel;
				rotatedFunction.bindings.push_back(Indium::BindingDescriptor { 2, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
				rotatedFunction.bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });
				rotatedFunction.bindings.push_back(Indium::BindingDescriptor { 1, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });

				std::string rotatedError;
				auto rotatedLib = device->newLibrary(add_mslc_spv, add_mslc_spv_len, rotated, &rotatedError);

				if (!rotatedLib) {
					std::cerr << "Control ERROR: the rotated reflection was rejected: " << rotatedError << std::endl;
					ok = false;
				} else {
					auto rotatedPSO = device->newComputePipelineState(rotatedLib->newFunction("add_arrays"));

					for (size_t i = 0; i < arrayLength; ++i) {
						result[i] = poison;
					}

					auto rotatedBuffer = commandQueue->commandBuffer();
					auto rotatedEncoder = rotatedBuffer->computeCommandEncoder();

					rotatedEncoder->setComputePipelineState(rotatedPSO);
					rotatedEncoder->setBuffer(bufA, 0, 0);
					rotatedEncoder->setBuffer(bufB, 0, 1);
					rotatedEncoder->setBuffer(bufResult, 0, 2);
					rotatedEncoder->dispatchThreads(Indium::Size { arrayLength, 1, 1 }, Indium::Size { threadGroupSize, 1, 1 });

					rotatedEncoder->endEncoding();
					rotatedBuffer->commit();
					rotatedBuffer->waitUntilCompleted();

					auto rotatedBad = mismatches(result, a, b, arrayLength);

					std::cout << "Negative control: " << rotatedBad << " of " << arrayLength
						<< " elements differ from a+b when the reflection's buffer indices are rotated" << std::endl;

					if (rotatedBad == 0) {
						std::cerr << "Control ERROR: the reflection's binding indices do not decide which buffer is written" << std::endl;
						ok = false;
					}
				}
			}
		}

		// The reflection is the only thing between a producer's document and a
		// descriptor set layout, so a reflection that cannot be used has to be
		// reported here rather than as a wrong binding later.
		Indium::LibraryReflection good;
		good.functions["add_arrays"].functionType = Indium::FunctionType::Kernel;
		good.functions["add_arrays"].bindings.push_back(Indium::BindingDescriptor { 0, 0, noIndex, Indium::BindingType::Buffer, Indium::TextureAccessType::Sample });

		auto expectRejected = [&](const char* what, const void* spirv, size_t length, const Indium::LibraryReflection& bad) {
			std::string message;
			auto rejected = device->newLibrary(spirv, length, bad, &message);

			if (rejected) {
				std::cerr << "Rejection ERROR: " << what << " was accepted" << std::endl;
				ok = false;
			} else if (message.empty()) {
				std::cerr << "Rejection ERROR: " << what << " was rejected without saying why" << std::endl;
				ok = false;
			} else {
				std::cout << "Rejected " << what << ": " << message << std::endl;
			}
		};

		expectRejected("a null module", nullptr, 0, good);
		expectRejected("a module shorter than a SPIR-V header", add_mslc_spv, 8, good);
		expectRejected("a module that is not a whole number of words", add_mslc_spv, add_mslc_spv_len - 1, good);
		expectRejected("a module that is not SPIR-V", "MTLBxxxx not a module", 20, good);
		expectRejected("a reflection of no functions", add_mslc_spv, add_mslc_spv_len, Indium::LibraryReflection());

		{
			Indium::LibraryReflection noStage = good;
			noStage.functions["add_arrays"].functionType = Indium::FunctionType::Invalid;

			expectRejected("a function whose stage was never set", add_mslc_spv, add_mslc_spv_len, noStage);
		}

		{
			Indium::LibraryReflection wrongName;
			wrongName.functions["no_such_function"].functionType = Indium::FunctionType::Kernel;
			wrongName.functions["no_such_function"].bindings = good.functions["add_arrays"].bindings;

			expectRejected("a function name the module does not carry", add_mslc_spv, add_mslc_spv_len, wrongName);
		}

		{
			// The address-block walk indexes the sampler states with this and
			// checks nothing, so a value past the end of the list has to be
			// refused here rather than read there.
			Indium::LibraryReflection badSampler = good;
			badSampler.functions["add_arrays"].bindings[0] = Indium::BindingDescriptor { noIndex, 0, 7, Indium::BindingType::Sampler, Indium::TextureAccessType::Sample };

			expectRejected("a sampler binding naming an embedded sampler that is not there", add_mslc_spv, add_mslc_spv_len, badSampler);
		}

		{
			// Two image bindings on one descriptor binding number is an invalid
			// set layout, and it is the producer's numbering that decides it.
			Indium::LibraryReflection duplicate = good;
			duplicate.functions["add_arrays"].bindings.push_back(Indium::BindingDescriptor { 1, 0, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Sample });
			duplicate.functions["add_arrays"].bindings.push_back(Indium::BindingDescriptor { 2, 0, noIndex, Indium::BindingType::Texture, Indium::TextureAccessType::Sample });

			expectRejected("two bindings claiming one descriptor binding", add_mslc_spv, add_mslc_spv_len, duplicate);
		}

		{
			// An unrecognised sampler field would otherwise reach the translation
			// in library.cpp, which substitutes a sampler state that works but is
			// not the one that was asked for.
			Indium::LibraryReflection oddSampler = good;
			oddSampler.functions["add_arrays"].bindings[0] = Indium::BindingDescriptor { noIndex, 0, 0, Indium::BindingType::Sampler, Indium::TextureAccessType::Sample };
			oddSampler.functions["add_arrays"].embeddedSamplers.push_back(Indium::EmbeddedSamplerDescriptor());
			oddSampler.functions["add_arrays"].embeddedSamplers[0].mipmapFilter = static_cast<Indium::EmbeddedSamplerDescriptor::MipFilter>(200);

			expectRejected("an embedded sampler field outside its enumeration", add_mslc_spv, add_mslc_spv_len, oddSampler);
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::cout << "Execution finished" << std::endl;

	return ok ? 0 : 1;
};
