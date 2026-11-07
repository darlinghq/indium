#include "../../src/iridium/air.cpp"
#include <llvm-c/IRReader.h>
#include <cstdio>
#include <cstring>

// Floating-point vector splats written as `zeroinitializer` or `splat (...)` reach the translator as
// vector-typed ConstantFP values on LLVM 23, and as ConstantAggregateZero/ConstantDataVector on older
// LLVM. Either way llvmValueToResultID must produce one OpConstantComposite whose components are the
// same OpConstant of the element type.

namespace {

struct Case {
	const char* type;       // vector type as written in IR
	const char* value;      // constant as written in IR
	unsigned lanes;
	unsigned scalarWords;   // words in the OpConstant literal
	uint64_t bits;          // expected literal bits of one lane
};

constexpr uint32_t opConstant = 43, opConstantComposite = 44;

bool check(LLVMContextRef context, const Case& c) {
	std::string ir = std::string("define ") + c.type + " @f() { ret " + c.type + " " + c.value + " }\n";
	LLVMMemoryBufferRef buffer = LLVMCreateMemoryBufferWithMemoryRangeCopy(ir.data(), ir.size(), "splat");
	LLVMModuleRef module = nullptr;
	char* error = nullptr;
	if (LLVMParseIRInContext(context, buffer, &module, &error)) {
		std::printf("FAIL cannot parse %s %s: %s\n", c.type, c.value, error);
		return false;
	}
	LLVMValueRef function = LLVMGetFirstFunction(module);
	LLVMValueRef constant = LLVMGetOperand(LLVMGetLastInstruction(LLVMGetEntryBasicBlock(function)), 0);

	Iridium::SPIRV::Builder builder;
	builder.setVersion(1, 5);
	builder.requireCapability(Iridium::SPIRV::Capability::Shader);
	builder.setAddressingModel(Iridium::SPIRV::AddressingModel::Logical);
	builder.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);

	Iridium::SPIRV::ResultID id = Iridium::SPIRV::ResultIDInvalid;
	try {
		id = llvmValueToResultID(builder, constant);
	} catch (const ImpossibleResultID& e) {
		std::printf("FAIL %s %s (value kind %d): %s\n", c.type, c.value, (int)LLVMGetValueKind(constant), e.what());
		LLVMDisposeModule(module);
		return false;
	}

	size_t size;
	void* bytes = builder.finalize(size);
	auto words = static_cast<const uint32_t*>(bytes);
	size_t total = size / 4;
	std::vector<uint32_t> lanes;
	for (size_t offset = 5; offset < total;) {
		unsigned count = words[offset] >> 16, opcode = words[offset] & 0xffff;
		if (!count || offset + count > total) {
			break;
		}
		if (opcode == opConstantComposite && words[offset + 2] == id) {
			lanes.assign(words + offset + 3, words + offset + count);
		}
		offset += count;
	}

	bool ok = lanes.size() == c.lanes;
	for (uint32_t lane : lanes) {
		ok = ok && lane == lanes[0];
	}
	bool literal = false;
	for (size_t offset = 5; ok && offset < total;) {
		unsigned count = words[offset] >> 16, opcode = words[offset] & 0xffff;
		if (opcode == opConstant && words[offset + 2] == lanes[0] && count == 3 + c.scalarWords) {
			uint64_t bits = words[offset + 3];
			if (c.scalarWords == 2) {
				bits |= uint64_t(words[offset + 4]) << 32;
			}
			literal = bits == c.bits;
		}
		offset += count;
	}
	ok = ok && literal;
	std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", c.type, c.value);
	std::free(bytes);
	LLVMDisposeModule(module);
	return ok;
}

}

int main() {
	if (!Iridium::DynamicLLVM::init()) {
		return 2;
	}
	LLVMContextRef context = LLVMContextCreate();
	const Case cases[] = {
		{"<3 x float>", "zeroinitializer", 3, 1, 0x00000000},
		{"<4 x float>", "splat (float 1.5)", 4, 1, 0x3fc00000},
		{"<2 x float>", "<float 1.5, float 1.5>", 2, 1, 0x3fc00000},
		{"<3 x double>", "zeroinitializer", 3, 2, 0},
		{"<2 x double>", "splat (double 1.5)", 2, 2, 0x3ff8000000000000ull},
		{"<4 x half>", "splat (half 0.5)", 4, 1, 0x3800},
	};
	unsigned failures = 0;
	for (const Case& c : cases) {
		failures += !check(context, c);
	}
	LLVMContextDispose(context);
	return failures ? 1 : 0;
}
