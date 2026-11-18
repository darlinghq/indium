// Copyright (C) 2026 Lubos Dolezel
//
// AIR comes from outside, so a call of an intrinsic with too few arguments, or with a first argument that is
// not the resource the intrinsic works on, must make the translation fail with an error instead of reading
// past the operands of the call. This authors a vertex function through LLVM's C API that calls each
// intrinsic named in kIntrinsics with every number of integer arguments below the one it takes and checks that
// each translation throws, and not with the message of an unknown function. Nothing here comes from
// third-party compiler output.

#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <llvm-c/BitWriter.h>

namespace {

// the intrinsics, and the number of arguments each takes: calls with fewer must be rejected
struct Intrinsic {
	const char* name;
	unsigned arguments;
};

const Intrinsic kIntrinsics[] = {
	{ "air.gather_texture_2d.v4f32", 3 },
	{ "air.gather_texture_2d_array.v4f32", 3 },
	{ "air.gather_texture_cube.v4f32", 3 },
	{ "air.gather_depth_2d.v4f32", 3 },
	{ "air.gather_depth_2d_array.v4f32", 3 },
	{ "air.gather_compare_depth_2d.f32", 3 },
	{ "air.gather_compare_depth_2d_array.f32", 3 },
	{ "air.gather_compare_depth_cube.f32", 3 },
	{ "air.get_width_texture_2d", 2 },
	{ "air.get_height_depth_cube", 2 },
	{ "air.get_depth_texture_3d", 2 },
	{ "air.get_array_size_texture_2d_array", 1 },
	{ "air.get_num_mip_levels_texture_2d", 1 },
	{ "air.get_num_samples_texture_2d_ms", 1 },
	{ "air.read_texture_2d_ms.v4f32", 3 },
	{ "air.read_texture_2d_ms_array.v4f32", 3 },
	{ "air.read_depth_2d_ms.f32", 3 },
	{ "air.atomic.global.add.f32", 3 },
	{ "air.atomic.global.max.u.i64", 3 },
	{ "air.atomic.global.cmpxchg.weak.i32", 3 },
	{ "air.atomic.fence", 3 },
	{ "air.simd_sum.u.i32", 1 },
	{ "air.simd_min.f32", 1 },
	{ "air.simd_prefix_exclusive_sum.u.i32", 1 },
	{ "air.simd_broadcast.u.i32", 2 },
	{ "air.simd_shuffle_down.u.i32", 2 },
	{ "air.simd_all", 1 },
	{ "air.simd_ballot.i64", 1 },
};

// translates a vertex function that calls `intrinsic` with `count` zero integers; returns the error message, or "" when it translated
std::string translate(const char* intrinsic, unsigned count) {
	auto c = LLVMContextCreate();
	auto m = LLVMModuleCreateWithNameInContext("authored-intrinsic-arguments", c);
	auto i32 = LLVMInt32TypeInContext(c);
	auto f32 = LLVMFloatTypeInContext(c);
	LLVMTypeRef members[] = { LLVMVectorType(f32, 4), LLVMInt8TypeInContext(c) };
	auto result = LLVMStructTypeInContext(c, members, 2, 0);
	std::vector<LLVMTypeRef> parameterTypes(count, i32);
	auto callee = LLVMAddFunction(m, intrinsic, LLVMFunctionType(result, parameterTypes.data(), count, 0));
	auto function = LLVMAddFunction(m, "authoredVertex", LLVMFunctionType(LLVMVoidTypeInContext(c), nullptr, 0, 0));
	auto writer = LLVMCreateBuilderInContext(c);
	LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
	std::vector<LLVMValueRef> arguments(count, LLVMConstInt(i32, 0, 0));
	LLVMBuildCall2(writer, LLVMGlobalGetValueType(callee), callee, arguments.data(), count, "called");
	LLVMBuildRetVoid(writer);
	LLVMValueRef root[] = { function, LLVMMDNodeInContext(c, nullptr, 0), LLVMMDNodeInContext(c, nullptr, 0) };
	LLVMAddNamedMetadataOperand(m, "air.vertex", LLVMMDNodeInContext(c, root, 3));
	auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);

	std::string message;
	try {
		Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Vertex, "authoredVertex", LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
		Iridium::SPIRV::Builder b;
		b.setVersion(1, 5);
		b.requireCapability(Iridium::SPIRV::Capability::Shader);
		b.setAddressingModel(Iridium::SPIRV::AddressingModel::Logical);
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
	unsigned failures = 0;
	for (auto [intrinsic, arguments]: kIntrinsics) {
		for (unsigned count = 0; count < arguments; ++count) {
			auto message = translate(intrinsic, count);
			bool ok = !message.empty() && message.find("TODO: support actual function calls") == std::string::npos;
			std::printf("%s %s with %u arguments: %s\n", ok ? "PASS" : "FAIL", intrinsic, count, message.empty() ? "translated" : message.c_str());
			failures += !ok;
		}
	}
	return failures ? 1 : 0;
}
