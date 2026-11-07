// Copyright (C) 2026 Darling Developers
//
// A vertex function's [[user(locnN)]] outputs and the fragment function inputs with the same
// attribute must get the same SPIR-V Location (N), whatever order the members are declared in, and
// a fragment [[color(N)]] output must be written to Location N. Translates AIR written in the form
// Metal's compiler emits and checks the Location of every Input and Output variable each entry point
// uses.

#include "../../src/iridium/air.cpp"
#include <llvm-c/BitWriter.h>
#include <llvm-c/IRReader.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace {

const char* const ir = R"AIR(
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

%VertexOut = type { <4 x float>, <4 x float>, float }
%FragmentOut = type { <4 x float> }

define %VertexOut @vertex_main() {
  %1 = insertvalue %VertexOut undef, <4 x float> zeroinitializer, 0
  %2 = insertvalue %VertexOut %1, <4 x float> <float 1.0, float 2.0, float 3.0, float 4.0>, 1
  %3 = insertvalue %VertexOut %2, float 0.5, 2
  ret %VertexOut %3
}

define %FragmentOut @fragment_main(float %b, <4 x float> %a) {
  %1 = insertelement <4 x float> undef, float %b, i32 0
  %2 = shufflevector <4 x float> %1, <4 x float> undef, <4 x i32> zeroinitializer
  %3 = fmul <4 x float> %a, %2
  %4 = insertvalue %FragmentOut undef, <4 x float> %3, 0
  ret %FragmentOut %4
}

!air.vertex = !{!0}
!air.fragment = !{!5}

!0 = !{ptr @vertex_main, !1, !{}}
!1 = !{!2, !3, !4}
!2 = !{!"air.position", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!3 = !{!"air.vertex_output", !"user(locn5)", !"air.arg_type_name", !"float4", !"air.arg_name", !"a"}
!4 = !{!"air.vertex_output", !"user(locn2)", !"air.arg_type_name", !"float", !"air.arg_name", !"b"}

!5 = !{ptr @fragment_main, !6, !8}
!6 = !{!7}
!7 = !{!"air.render_target", i32 1, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
!8 = !{!9, !10}
!9 = !{i32 0, !"air.fragment_input", !"user(locn2)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float", !"air.arg_name", !"b"}
!10 = !{i32 1, !"air.fragment_input", !"user(locn5)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"a"}
)AIR";

constexpr uint32_t opEntryPoint = 15, opVariable = 59, opDecorate = 71, decorationLocation = 30;
constexpr uint32_t storageInput = 1, storageOutput = 3;

void append(std::vector<uint8_t>& out, const void* data, size_t size) {
	auto bytes = static_cast<const uint8_t*>(data);
	out.insert(out.end(), bytes, bytes + size);
}

template<typename T>
void appendInteger(std::vector<uint8_t>& out, T value) {
	append(out, &value, sizeof(value));
}

void appendTag(std::vector<uint8_t>& out, const char* name, const std::vector<uint8_t>& payload) {
	append(out, name, 4);
	appendInteger<uint16_t>(out, payload.size());
	append(out, payload.data(), payload.size());
}

// The MTLB container AIR::Library reads, with every function entry pointing at the same module.
std::vector<uint8_t> makeLibrary(const std::vector<uint8_t>& bitcode, const std::vector<std::pair<std::string, uint8_t>>& functions) {
	std::vector<uint8_t> list;
	appendInteger<uint32_t>(list, functions.size());
	for (auto& [name, type]: functions) {
		std::vector<uint8_t> group, payload;
		payload.assign(name.begin(), name.end());
		payload.push_back(0);
		appendTag(group, "NAME", payload);
		appendTag(group, "TYPE", { type });
		payload.clear();
		appendInteger<uint64_t>(payload, bitcode.size());
		appendTag(group, "MDSZ", payload);
		appendTag(group, "OFFT", std::vector<uint8_t>(24, 0));
		append(group, "ENDT", 4);
		appendInteger<uint32_t>(list, group.size() + 4);
		append(list, group.data(), group.size());
	}

	constexpr uint64_t headerSize = 88;
	uint64_t bitcodeOffset = headerSize + list.size();
	std::vector<uint8_t> library;
	append(library, "MTLB", 4);
	appendInteger<uint16_t>(library, 0x8001);
	appendInteger<uint16_t>(library, 1);
	appendInteger<uint16_t>(library, 2);
	appendInteger<uint8_t>(library, 0);
	appendInteger<uint8_t>(library, 0x81);
	appendInteger<uint16_t>(library, 10);
	appendInteger<uint16_t>(library, 15);
	for (uint64_t field: { bitcodeOffset + bitcode.size(), headerSize, uint64_t(list.size()), bitcodeOffset, uint64_t(0), bitcodeOffset, uint64_t(0), bitcodeOffset, uint64_t(bitcode.size()) }) {
		appendInteger<uint64_t>(library, field);
	}
	append(library, list.data(), list.size());
	append(library, bitcode.data(), bitcode.size());
	return library;
}

}

int main() {
	if (!Iridium::DynamicLLVM::init()) {
		return 2;
	}

	LLVMContextRef context = LLVMContextCreate();
	LLVMMemoryBufferRef source = LLVMCreateMemoryBufferWithMemoryRangeCopy(ir, std::strlen(ir), "interface-locations");
	LLVMModuleRef module = nullptr;
	char* error = nullptr;
	if (LLVMParseIRInContext(context, source, &module, &error)) {
		std::printf("FAIL cannot parse the AIR: %s\n", error);
		return 1;
	}
	LLVMMemoryBufferRef bitcodeBuffer = LLVMWriteBitcodeToMemoryBuffer(module);
	auto bitcodeStart = reinterpret_cast<const uint8_t*>(LLVMGetBufferStart(bitcodeBuffer));
	std::vector<uint8_t> bitcode(bitcodeStart, bitcodeStart + LLVMGetBufferSize(bitcodeBuffer));
	LLVMDisposeMemoryBuffer(bitcodeBuffer);
	LLVMDisposeModule(module);
	LLVMContextDispose(context);

	auto library = makeLibrary(bitcode, { { "vertex_main", 0 }, { "fragment_main", 1 } });

	std::vector<uint32_t> words;
	try {
		Iridium::AIR::Library lib(library.data(), library.size());
		Iridium::SPIRV::Builder builder;
		Iridium::OutputInfo outputInfo;
		lib.buildModule(builder, outputInfo);
		size_t size = 0;
		auto bytes = static_cast<uint32_t*>(builder.finalize(size));
		words.assign(bytes, bytes + size / 4);
		std::free(bytes);
	} catch (const std::exception& e) {
		std::printf("FAIL translation: %s\n", e.what());
		return 1;
	}

	std::map<uint32_t, uint32_t> storageClasses, locations;
	std::map<std::string, std::vector<uint32_t>> interfaces;
	for (size_t offset = 5; offset < words.size();) {
		uint32_t count = words[offset] >> 16, opcode = words[offset] & 0xffff;
		if (count == 0 || offset + count > words.size()) {
			std::printf("FAIL malformed SPIR-V\n");
			return 1;
		}
		if (opcode == opVariable) {
			storageClasses[words[offset + 2]] = words[offset + 3];
		} else if (opcode == opDecorate && count >= 4 && words[offset + 2] == decorationLocation) {
			locations[words[offset + 1]] = words[offset + 3];
		} else if (opcode == opEntryPoint) {
			auto name = reinterpret_cast<const char*>(&words[offset + 3]);
			size_t nameWords = std::strlen(name) / 4 + 1;
			interfaces[name].assign(words.begin() + offset + 3 + nameWords, words.begin() + offset + count);
		}
		offset += count;
	}

	auto located = [&](const char* function, uint32_t storageClass) {
		std::set<uint32_t> result;
		for (auto id: interfaces[function]) {
			if (storageClasses[id] == storageClass && locations.count(id)) {
				result.insert(locations[id]);
			}
		}
		return result;
	};

	struct Expectation {
		const char* function;
		uint32_t storageClass;
		const char* what;
		std::set<uint32_t> locations;
	} expectations[] = {
		{ "vertex_main", storageOutput, "vertex outputs", { 2, 5 } },
		{ "fragment_main", storageInput, "fragment inputs", { 2, 5 } },
		{ "fragment_main", storageOutput, "fragment outputs", { 1 } },
	};

	unsigned failures = 0;
	for (auto& expectation: expectations) {
		auto got = located(expectation.function, expectation.storageClass);
		bool ok = got == expectation.locations;
		std::printf("%s %s:", ok ? "PASS" : "FAIL", expectation.what);
		for (auto location: got) {
			std::printf(" %u", location);
		}
		std::printf("\n");
		failures += !ok;
	}

	return failures ? 1 : 0;
}
