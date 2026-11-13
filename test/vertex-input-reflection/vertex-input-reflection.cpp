// Copyright (C) 2026 Lubos Dolezel
//
// A vertex function's [[stage_in]] attributes are reported in FunctionInfo::arguments as
// BindingType::VertexInput entries, keyed by the attribute index the shader declares, next to the
// buffer arguments but never mistaken for them; a vertex input whose descriptor names no location is
// rejected instead of being given an index. Translates AIR written in the form Metal's compiler
// emits (no GPU needed).
//
// With a file name argument the program also writes the second module as a metallib (a vertex
// function with attributes, one without, a fragment function and a kernel), which Metal's guest
// test for MTLFunction.vertexAttributes embeds.

#include "../../src/iridium/air.cpp"
#include <llvm-c/BitWriter.h>
#include <llvm-c/IRReader.h>
#include <cstdio>
#include <cstring>

namespace {

const char* const header = R"AIR(
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

%VertexOut = type { <4 x float> }

define %VertexOut @vertex_main(<4 x float> %position, <2 x i32> %ids, i32 %vid, ptr addrspace(1) %scale) {
  %1 = insertvalue %VertexOut undef, <4 x float> %position, 0
  ret %VertexOut %1
}

!air.vertex = !{!0}

!0 = !{ptr @vertex_main, !1, !3}
!1 = !{!2}
!2 = !{!"air.position", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
)AIR";

// attribute 6 comes first and attribute 2 second: neither order nor position is the index
const char* const parameters = R"AIR(
!3 = !{!4, !5, !6, !7}
!4 = !{i32 0, !"air.vertex_input", !"air.location_index", i32 6, i32 1, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!5 = !{i32 1, !"air.vertex_input", !"air.location_index", i32 2, i32 1, !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"uint2", !"air.arg_name", !"ids"}
!6 = !{i32 2, !"air.vertex_id"}
!7 = !{i32 3, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"scale"}
)AIR";

const char* const parametersWithoutLocation = R"AIR(
!3 = !{!4}
!4 = !{i32 0, !"air.vertex_input", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
)AIR";

// A library of the four function kinds. The attributes sit at locations 0, 3 and 5.
const char* const libraryIR = R"AIR(
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

%VertexOut = type { <4 x float>, <4 x float> }
%FragmentOut = type { <4 x float> }

define %VertexOut @vertex_with_inputs(<4 x float> %position, <2 x i32> %ids, float %weight) {
  %1 = insertvalue %VertexOut undef, <4 x float> %position, 0
  %2 = insertvalue %VertexOut %1, <4 x float> %position, 1
  ret %VertexOut %2
}

define %VertexOut @vertex_without_inputs(i32 %vid) {
  %1 = insertvalue %VertexOut undef, <4 x float> zeroinitializer, 0
  %2 = insertvalue %VertexOut %1, <4 x float> zeroinitializer, 1
  ret %VertexOut %2
}

define %FragmentOut @fragment_main(<4 x float> %color) {
  %1 = insertvalue %FragmentOut undef, <4 x float> %color, 0
  ret %FragmentOut %1
}

define void @kernel_main(ptr addrspace(1) %data, i32 %tid) {
  store float 0.0, ptr addrspace(1) %data, align 4
  ret void
}

!air.vertex = !{!0, !20}
!air.fragment = !{!30}
!air.kernel = !{!40}

!0 = !{ptr @vertex_with_inputs, !1, !3}
!1 = !{!2, !9}
!2 = !{!"air.position", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!9 = !{!"air.vertex_output", !"user(locn0)", !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
!3 = !{!4, !5, !6}
!4 = !{i32 0, !"air.vertex_input", !"air.location_index", i32 0, i32 1, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!5 = !{i32 1, !"air.vertex_input", !"air.location_index", i32 3, i32 1, !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"uint2", !"air.arg_name", !"ids"}
!6 = !{i32 2, !"air.vertex_input", !"air.location_index", i32 5, i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"weight"}

!20 = !{ptr @vertex_without_inputs, !21, !23}
!21 = !{!22, !9}
!22 = !{!"air.position", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!23 = !{!24}
!24 = !{i32 0, !"air.vertex_id"}

!30 = !{ptr @fragment_main, !31, !33}
!31 = !{!32}
!32 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"out"}
!33 = !{!34}
!34 = !{i32 0, !"air.fragment_input", !"user(locn0)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}

!40 = !{ptr @kernel_main, !41, !42}
!41 = !{}
!42 = !{!43, !44}
!43 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"data"}
!44 = !{i32 1, !"air.thread_position_in_grid"}
)AIR";

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

// The MTLB library for `ir`, with the given functions.
std::vector<uint8_t> libraryFor(const std::string& ir, const std::vector<std::pair<std::string, uint8_t>>& functions) {
	LLVMContextRef context = LLVMContextCreate();
	LLVMMemoryBufferRef source = LLVMCreateMemoryBufferWithMemoryRangeCopy(ir.data(), ir.size(), "vertex-input-reflection");
	LLVMModuleRef module = nullptr;
	char* error = nullptr;
	if (LLVMParseIRInContext(context, source, &module, &error)) {
		std::printf("FAIL cannot parse the AIR: %s\n", error);
		std::exit(1);
	}
	LLVMMemoryBufferRef bitcodeBuffer = LLVMWriteBitcodeToMemoryBuffer(module);
	auto bitcodeStart = reinterpret_cast<const uint8_t*>(LLVMGetBufferStart(bitcodeBuffer));
	std::vector<uint8_t> bitcode(bitcodeStart, bitcodeStart + LLVMGetBufferSize(bitcodeBuffer));
	LLVMDisposeMemoryBuffer(bitcodeBuffer);
	LLVMDisposeModule(module);
	LLVMContextDispose(context);
	return makeLibrary(bitcode, functions);
}

// Translates the library; false (with the reason printed) when translation throws.
bool translateLibrary(const std::vector<uint8_t>& library, Iridium::OutputInfo& outputInfo) {
	try {
		Iridium::AIR::Library lib(library.data(), library.size());
		Iridium::SPIRV::Builder builder;
		lib.buildModule(builder, outputInfo);
		size_t size = 0;
		std::free(builder.finalize(size));
	} catch (const std::exception& e) {
		std::printf("translation threw: %s\n", e.what());
		return false;
	}
	return true;
}

unsigned failures = 0;

void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

}

int main(int argc, char** argv) {
	if (!Iridium::DynamicLLVM::init()) {
		return 2;
	}

	Iridium::OutputInfo info;
	bool translated = translateLibrary(libraryFor(std::string(header) + parameters, { { "vertex_main", 0 } }), info);
	check(translated, "a vertex function with stage-in attributes translates");
	if (translated) {
		auto& arguments = info.functionInfos["vertex_main"].arguments;
		check(arguments.size() == 3, "two vertex inputs and one buffer are reflected (vertex_id is not an argument)");
		if (arguments.size() == 3) {
			check(arguments[0].type == Iridium::BindingType::VertexInput && arguments[0].index == 6
				&& arguments[0].name == "position" && arguments[0].typeName == "float4",
				"first vertex input: attribute 6, position, float4");
			check(arguments[1].type == Iridium::BindingType::VertexInput && arguments[1].index == 2
				&& arguments[1].name == "ids" && arguments[1].typeName == "uint2",
				"second vertex input: attribute 2, ids, uint2");
			check(arguments[2].type == Iridium::BindingType::Buffer && arguments[2].index == 0
				&& arguments[2].name == "scale", "the buffer stays a buffer at index 0");
		}
	}

	Iridium::OutputInfo bad;
	check(!translateLibrary(libraryFor(std::string(header) + parametersWithoutLocation, { { "vertex_main", 0 } }), bad),
		"a vertex input without a location index is rejected");

	// the library for Metal's guest test
	auto library = libraryFor(libraryIR, { { "vertex_with_inputs", 0 }, { "vertex_without_inputs", 0 }, { "fragment_main", 1 }, { "kernel_main", 2 } });
	Iridium::OutputInfo libraryInfo;
	check(translateLibrary(library, libraryInfo), "the four-function library translates");
	if (translated) {
		auto& withInputs = libraryInfo.functionInfos["vertex_with_inputs"].arguments;
		check(withInputs.size() == 3 && withInputs[0].index == 0 && withInputs[1].index == 3 && withInputs[2].index == 5
			&& withInputs[1].typeName == "uint2", "the vertex function lists attributes 0, 3 and 5");
		check(libraryInfo.functionInfos["vertex_without_inputs"].arguments.empty(), "a vertex function without stage-in has no arguments");
		auto& kernel = libraryInfo.functionInfos["kernel_main"].arguments;
		check(kernel.size() == 1 && kernel[0].type == Iridium::BindingType::Buffer, "the kernel's only argument is its buffer");
	}
	if (argc > 1) {
		FILE* file = std::fopen(argv[1], "wb");
		if (!file || std::fwrite(library.data(), 1, library.size(), file) != library.size() || std::fclose(file) != 0) {
			std::printf("FAIL cannot write %s\n", argv[1]);
			return 1;
		}
	}

	return failures ? 1 : 0;
}
