// Copyright (C) 2026 Lubos Dolezel
//
// Calls from a kernel to helper functions are translated by inlining the helpers, and AIR comes from outside,
// so a call graph that cannot be inlined must fail the translation with an error instead of crashing or
// looping. This authors modules through LLVM's C API (nothing here comes from compiler output) and checks
// that calls of every shape the language allows translate, and that recursion (direct, mutual, through the
// kernel), indirect calls, calls without a body, calls whose type disagrees with the callee, call chains that
// are too deep and call trees that expand to too many instructions are all refused.

#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <llvm-c/BitWriter.h>

namespace {

struct Module {
	LLVMContextRef c;
	LLVMModuleRef m;
	LLVMBuilderRef b;
	LLVMTypeRef i32, ptr, voidType;
	LLVMValueRef kernel;
	LLVMTypeRef kernelType;

	Module() {
		c = LLVMContextCreate();
		m = LLVMModuleCreateWithNameInContext("authored-calls", c);
		b = LLVMCreateBuilderInContext(c);
		i32 = LLVMInt32TypeInContext(c);
		ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 1);
		voidType = LLVMVoidTypeInContext(c);
		kernelType = LLVMFunctionType(voidType, &ptr, 1, 0);
		kernel = LLVMAddFunction(m, "authoredKernel", kernelType);
	}
	~Module() {
		LLVMDisposeBuilder(b);
		LLVMDisposeModule(m);
		LLVMContextDispose(c);
	}

	LLVMMetadataRef str(const char* s) { return LLVMMDStringInContext2(c, s, std::strlen(s)); }
	LLVMMetadataRef number(int v) { return LLVMValueAsMetadata(LLVMConstInt(i32, v, 0)); }
	LLVMMetadataRef node(std::vector<LLVMMetadataRef> ops) { return LLVMMDNodeInContext2(c, ops.data(), ops.size()); }

	// a function with a body that has not been written yet
	LLVMValueRef add(const char* name, LLVMTypeRef type, bool noinline = true) {
		auto f = LLVMAddFunction(m, name, type);
		if (noinline) {
			LLVMAddAttributeAtIndex(f, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, LLVMGetEnumAttributeKindForName("noinline", 8), 0));
		}
		LLVMSetLinkage(f, LLVMInternalLinkage);
		return f;
	}
	void begin(LLVMValueRef f) { LLVMPositionBuilderAtEnd(b, LLVMAppendBasicBlockInContext(c, f, "entry")); }
	LLVMValueRef call(LLVMValueRef f, std::vector<LLVMValueRef> args) {
		return LLVMBuildCall2(b, LLVMGlobalGetValueType(f), f, args.data(), args.size(), "");
	}
	// ends the kernel and returns the bitcode of the module
	LLVMMemoryBufferRef finish() {
		LLVMBuildRetVoid(b);
		auto arg = node({ number(0), str("air.buffer"), str("air.location_index"), number(0), number(1), str("air.read_write"),
			str("air.address_space"), number(1), str("air.arg_type_size"), number(4), str("air.arg_type_align_size"), number(4),
			str("air.arg_type_name"), str("int"), str("air.arg_name"), str("p") });
		auto root = node({ LLVMValueAsMetadata(kernel), node({}), node({ arg }) });
		LLVMAddNamedMetadataOperand(m, "air.kernel", LLVMMetadataAsValue(c, root));
		return LLVMWriteBitcodeToMemoryBuffer(m);
	}
	LLVMValueRef param() { return LLVMGetParam(kernel, 0); }
	LLVMValueRef gep(LLVMValueRef p, int index) {
		auto idx = LLVMConstInt(i32, index, 0);
		return LLVMBuildGEP2(b, i32, p, &idx, 1, "");
	}
};

struct Case {
	const char* what;
	std::function<void(Module&)> build;
	// the text the error has to contain; empty when the translation has to succeed
	const char* expected;
};

// returns the error of the translation, or "" when it succeeded
std::string translate(const Case& test) {
	Module module;
	test.build(module);
	auto bitcode = module.finish();
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
	return message;
}

// void store(ptr p, i32 v) { p[1] = v; }
LLVMValueRef addStore(Module& m, const char* name = "store") {
	LLVMTypeRef params[] = { m.ptr, m.i32 };
	auto f = m.add(name, LLVMFunctionType(m.voidType, params, 2, 0));
	m.begin(f);
	LLVMBuildStore(m.b, LLVMGetParam(f, 1), m.gep(LLVMGetParam(f, 0), 1));
	LLVMBuildRetVoid(m.b);
	return f;
}

// i32 add(i32 x) { return x + 1; }
LLVMValueRef addIncrement(Module& m, const char* name = "increment") {
	auto f = m.add(name, LLVMFunctionType(m.i32, &m.i32, 1, 0));
	m.begin(f);
	LLVMBuildRet(m.b, LLVMBuildAdd(m.b, LLVMGetParam(f, 0), LLVMConstInt(m.i32, 1, 0), ""));
	return f;
}

}

int main() {
	if (!Iridium::DynamicLLVM::init()) {
		return 2;
	}

	std::vector<Case> cases {
		{ "a kernel without calls", [](Module& m) { m.begin(m.kernel); }, "" },
		{ "a call of a noinline helper that stores through a pointer parameter", [](Module& m) {
			auto f = addStore(m);
			m.begin(m.kernel);
			m.call(f, { m.param(), LLVMConstInt(m.i32, 7, 0) });
		}, "" },
		{ "a call of a helper that returns a value", [](Module& m) {
			auto f = addIncrement(m);
			m.begin(m.kernel);
			auto r = m.call(f, { LLVMConstInt(m.i32, 4, 0) });
			LLVMBuildStore(m.b, r, m.param());
		}, "" },
		{ "one helper called twice and a nested call", [](Module& m) {
			auto inc = addIncrement(m);
			auto twice = m.add("twice", LLVMFunctionType(m.i32, &m.i32, 1, 0));
			m.begin(twice);
			LLVMBuildRet(m.b, m.call(inc, { m.call(inc, { LLVMGetParam(twice, 0) }) }));
			m.begin(m.kernel);
			LLVMBuildStore(m.b, m.call(twice, { LLVMConstInt(m.i32, 1, 0) }), m.param());
			LLVMBuildStore(m.b, m.call(inc, { LLVMConstInt(m.i32, 2, 0) }), m.gep(m.param(), 1));
		}, "" },
		{ "a helper without the noinline attribute", [](Module& m) {
			auto f = m.add("plain", LLVMFunctionType(m.i32, &m.i32, 1, 0), false);
			m.begin(f);
			LLVMBuildRet(m.b, LLVMGetParam(f, 0));
			m.begin(m.kernel);
			LLVMBuildStore(m.b, m.call(f, { LLVMConstInt(m.i32, 1, 0) }), m.param());
		}, "" },
		{ "a helper that loops over a pointer parameter", [](Module& m) {
			LLVMTypeRef params[] = { m.ptr, m.i32 };
			auto f = m.add("sum", LLVMFunctionType(m.i32, params, 2, 0));
			auto entry = LLVMAppendBasicBlockInContext(m.c, f, "entry");
			auto loop = LLVMAppendBasicBlockInContext(m.c, f, "loop");
			auto done = LLVMAppendBasicBlockInContext(m.c, f, "done");
			LLVMPositionBuilderAtEnd(m.b, entry);
			LLVMBuildBr(m.b, loop);
			LLVMPositionBuilderAtEnd(m.b, loop);
			auto k = LLVMBuildPhi(m.b, m.i32, "k");
			auto s = LLVMBuildPhi(m.b, m.i32, "s");
			auto sNext = LLVMBuildAdd(m.b, s, LLVMBuildLoad2(m.b, m.i32, LLVMBuildGEP2(m.b, m.i32, LLVMGetParam(f, 0), &k, 1, ""), ""), "");
			auto kNext = LLVMBuildAdd(m.b, k, LLVMConstInt(m.i32, 1, 0), "");
			auto cond = LLVMBuildICmp(m.b, LLVMIntULT, kNext, LLVMGetParam(f, 1), "");
			LLVMBuildCondBr(m.b, cond, loop, done);
			LLVMValueRef kIn[] = { LLVMConstInt(m.i32, 0, 0), kNext };
			LLVMValueRef sIn[] = { LLVMConstInt(m.i32, 0, 0), sNext };
			LLVMBasicBlockRef from[] = { entry, loop };
			LLVMAddIncoming(k, kIn, from, 2);
			LLVMAddIncoming(s, sIn, from, 2);
			LLVMPositionBuilderAtEnd(m.b, done);
			LLVMBuildRet(m.b, sNext);
			m.begin(m.kernel);
			LLVMBuildStore(m.b, m.call(f, { m.param(), LLVMConstInt(m.i32, 4, 0) }), m.gep(m.param(), 8));
		}, "" },
		{ "a function that calls itself", [](Module& m) {
			auto f = m.add("self", LLVMFunctionType(m.i32, &m.i32, 1, 0));
			m.begin(f);
			LLVMBuildRet(m.b, m.call(f, { LLVMGetParam(f, 0) }));
			m.begin(m.kernel);
			m.call(f, { LLVMConstInt(m.i32, 1, 0) });
		}, "Recursive call" },
		{ "two functions that call each other", [](Module& m) {
			auto type = LLVMFunctionType(m.i32, &m.i32, 1, 0);
			auto a = m.add("ping", type);
			auto b = m.add("pong", type);
			m.begin(a);
			LLVMBuildRet(m.b, m.call(b, { LLVMGetParam(a, 0) }));
			m.begin(b);
			LLVMBuildRet(m.b, m.call(a, { LLVMGetParam(b, 0) }));
			m.begin(m.kernel);
			m.call(a, { LLVMConstInt(m.i32, 1, 0) });
		}, "Recursive call" },
		{ "a helper that calls the kernel", [](Module& m) {
			auto f = m.add("back", LLVMFunctionType(m.voidType, &m.ptr, 1, 0));
			m.begin(f);
			m.call(m.kernel, { LLVMGetParam(f, 0) });
			LLVMBuildRetVoid(m.b);
			m.begin(m.kernel);
			m.call(f, { m.param() });
		}, "Recursive call" },
		{ "a call of a function that has no body", [](Module& m) {
			auto f = LLVMAddFunction(m.m, "missing", LLVMFunctionType(m.voidType, nullptr, 0, 0));
			m.begin(m.kernel);
			m.call(f, {});
		}, "has no body" },
		{ "a call through a pointer", [](Module& m) {
			m.begin(m.kernel);
			LLVMBuildCall2(m.b, LLVMFunctionType(m.voidType, nullptr, 0, 0), m.param(), nullptr, 0, "");
		}, "Indirect calls" },
		{ "a call whose type differs from the callee's", [](Module& m) {
			auto f = addIncrement(m);
			m.begin(m.kernel);
			LLVMBuildCall2(m.b, LLVMFunctionType(m.voidType, nullptr, 0, 0), f, nullptr, 0, "");
		}, "does not match" },
		{ "a chain of calls nested too deeply", [](Module& m) {
			std::vector<LLVMValueRef> chain;
			auto type = LLVMFunctionType(m.voidType, nullptr, 0, 0);
			for (int i = 0; i < 300; i++) {
				chain.push_back(m.add(("level" + std::to_string(i)).c_str(), type));
			}
			for (int i = 0; i < 300; i++) {
				m.begin(chain[i]);
				if (i + 1 < 300) {
					m.call(chain[i + 1], {});
				}
				LLVMBuildRetVoid(m.b);
			}
			m.begin(m.kernel);
			m.call(chain[0], {});
		}, "nested too deeply" },
		{ "a call tree that expands to too many instructions", [](Module& m) {
			// every level calls the next one twice, so 40 levels would expand to 2^40 calls
			std::vector<LLVMValueRef> chain;
			auto type = LLVMFunctionType(m.voidType, nullptr, 0, 0);
			for (int i = 0; i < 40; i++) {
				chain.push_back(m.add(("fan" + std::to_string(i)).c_str(), type));
			}
			for (int i = 0; i < 40; i++) {
				m.begin(chain[i]);
				if (i + 1 < 40) {
					m.call(chain[i + 1], {});
					m.call(chain[i + 1], {});
				}
				LLVMBuildRetVoid(m.b);
			}
			m.begin(m.kernel);
			m.call(chain[0], {});
		}, "too large" },
	};

	unsigned failures = 0;
	for (auto& test: cases) {
		auto message = translate(test);
		bool ok = test.expected[0] ? message.find(test.expected) != std::string::npos : message.empty();
		std::printf("%s %s: %s\n", ok ? "PASS" : "FAIL", test.what, message.empty() ? "translated" : message.c_str());
		failures += !ok;
	}
	return failures ? 1 : 0;
}
