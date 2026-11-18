// Copyright (C) 2026 Lubos Dolezel
//
// AIR comes from outside, so descriptions of kernel parameters that are cut short, hold the wrong kind of
// operand or name a texture type that cannot be parsed must make the translation fail with an error, not read
// past the operands or the type name. This authors a compute function through LLVM's C API whose only
// parameter is described by the node each case supplies, and checks that the description of a well-formed
// texture translates and that every malformed one throws. Nothing here comes from third-party compiler output.

#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <llvm-c/BitWriter.h>

namespace {

struct Case {
	const char* what;
	// builds the description of the parameter
	std::function<LLVMMetadataRef(LLVMContextRef)> description;
	bool valid;
};

LLVMMetadataRef str(LLVMContextRef c, const char* s) {
	return LLVMMDStringInContext2(c, s, std::strlen(s));
}

LLVMMetadataRef number(LLVMContextRef c, int value) {
	return LLVMValueAsMetadata(LLVMConstInt(LLVMInt32TypeInContext(c), value, 0));
}

LLVMMetadataRef node(LLVMContextRef c, std::vector<LLVMMetadataRef> operands) {
	return LLVMMDNodeInContext2(c, operands.data(), operands.size());
}

// the description of a texture parameter with the given type name (null: no air.arg_type_name) and location index (-1: none)
LLVMMetadataRef texture(LLVMContextRef c, const char* typeName, int location = 0) {
	std::vector<LLVMMetadataRef> operands { number(c, 0), str(c, "air.texture") };
	if (location >= 0) {
		operands.insert(operands.end(), { str(c, "air.location_index"), number(c, location), number(c, 1) });
	}
	operands.push_back(str(c, "air.sample"));
	if (typeName) {
		operands.insert(operands.end(), { str(c, "air.arg_type_name"), str(c, typeName) });
	}
	operands.insert(operands.end(), { str(c, "air.arg_name"), str(c, "t") });
	return node(c, operands);
}

// returns the error of the translation, or "" when it succeeded
std::string translate(const Case& test) {
	auto c = LLVMContextCreate();
	auto m = LLVMModuleCreateWithNameInContext("authored-parameters", c);
	auto pointer = LLVMPointerType(LLVMInt8TypeInContext(c), 1);
	auto function = LLVMAddFunction(m, "authoredKernel", LLVMFunctionType(LLVMVoidTypeInContext(c), &pointer, 1, 0));
	auto writer = LLVMCreateBuilderInContext(c);
	LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
	LLVMBuildRetVoid(writer);
	auto root = node(c, { LLVMValueAsMetadata(function), node(c, {}), node(c, { test.description(c) }) });
	LLVMAddNamedMetadataOperand(m, "air.kernel", LLVMMetadataAsValue(c, root));
	auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);

	std::string message;
	try {
		Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Kernel, "authoredKernel", LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
		Iridium::SPIRV::Builder b;
		b.setVersion(1, 5);
		b.requireCapability(Iridium::SPIRV::Capability::Shader);
		b.requireCapability(Iridium::SPIRV::Capability::PhysicalStorageBufferAddresses);
		b.setAddressingModel(Iridium::SPIRV::AddressingModel::PhysicalStorageBuffer64);
		b.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);
		Iridium::OutputInfo info;
		translated.analyze(b, info);
	} catch (const std::exception& e) {
		message = e.what();
	}
	LLVMDisposeMemoryBuffer(bitcode);
	LLVMDisposeBuilder(writer);
	LLVMDisposeModule(m);
	LLVMContextDispose(c);
	return message;
}

}

int main() {
	if (!Iridium::DynamicLLVM::init()) {
		return 2;
	}

	std::vector<Case> cases {
		{ "a well-formed texture", [](LLVMContextRef c) { return texture(c, "texture2d<float, sample>"); }, true },
		{ "a texture type name without angle brackets", [](LLVMContextRef c) { return texture(c, "texture2d"); }, false },
		{ "a texture type name cut off after the bracket", [](LLVMContextRef c) { return texture(c, "texture2d<"); }, false },
		{ "a texture type name without a comma", [](LLVMContextRef c) { return texture(c, "texture2d<float>"); }, false },
		{ "a texture type name cut off after the comma", [](LLVMContextRef c) { return texture(c, "texture2d<float,"); }, false },
		{ "a texture type name with only spaces after the comma", [](LLVMContextRef c) { return texture(c, "texture2d<float,   "); }, false },
		{ "a texture type name without the closing bracket", [](LLVMContextRef c) { return texture(c, "texture2d<float, sample"); }, false },
		{ "a texture type name with the brackets reversed", [](LLVMContextRef c) { return texture(c, "texture2d>float, sample<"); }, false },
		{ "a texture type name that is only a bracket", [](LLVMContextRef c) { return texture(c, ">"); }, false },
		{ "an empty texture type name", [](LLVMContextRef c) { return texture(c, ""); }, false },
		{ "a texture without a type name", [](LLVMContextRef c) { return texture(c, nullptr); }, false },
		{ "a texture without a location index", [](LLVMContextRef c) { return texture(c, "texture2d<float, sample>", -1); }, false },
		{ "a description with only an index", [](LLVMContextRef c) { return node(c, { number(c, 0) }); }, false },
		{ "an empty description", [](LLVMContextRef c) { return node(c, {}); }, false },
		{ "a description whose kind is a number", [](LLVMContextRef c) { return node(c, { number(c, 0), number(c, 1) }); }, false },
		{ "a description with a null kind", [](LLVMContextRef c) { return node(c, { number(c, 0), nullptr }); }, false },
		{ "a buffer without a location index", [](LLVMContextRef c) {
			return node(c, { number(c, 0), str(c, "air.buffer"), str(c, "air.arg_type_name"), str(c, "float") });
		}, false },
		{ "a location index without a value", [](LLVMContextRef c) {
			return node(c, { number(c, 0), str(c, "air.texture"), str(c, "air.arg_type_name"), str(c, "texture2d<float, sample>"), str(c, "air.location_index") });
		}, false },
		{ "a location index that is not a number", [](LLVMContextRef c) {
			return node(c, { number(c, 0), str(c, "air.texture"), str(c, "air.arg_type_name"), str(c, "texture2d<float, sample>"), str(c, "air.location_index"), str(c, "x"), number(c, 1) });
		}, false },
	};

	unsigned failures = 0;
	for (auto& test: cases) {
		auto message = translate(test);
		bool ok = test.valid ? message.empty() : !message.empty();
		std::printf("%s %s: %s\n", ok ? "PASS" : "FAIL", test.what, message.empty() ? "translated" : message.c_str());
		failures += !ok;
	}
	return failures ? 1 : 0;
}
