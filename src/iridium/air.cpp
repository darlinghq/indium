#include <optional>
#include <set>
#include <unordered_map>
#include <iridium/air.hpp>
#include <iridium/bits.hpp>
#include <iridium/spirv.hpp>
#include <iridium/dynamic-llvm.hpp>

#include <llvm-c/BitReader.h>
#include <llvm/Config/llvm-config.h>
#include <algorithm>

namespace DynamicLLVM = Iridium::DynamicLLVM;

// special thanks to https://github.com/YuAo/MetalLibraryArchive for information on the library format

// we use LLVM's C API because it's much more stable than the C++ API and it's good enough for our needs

// Calls to helper functions are resolved by inlining the callee into the kernel before translation, so that
// every pointer keeps the type the kernel's argument description gives it and the translator only ever sees
// one function. Metal Shading Language has no recursion and no function pointers, so a module that has either
// (or a call that has no body to inline) is not valid AIR and is refused here. The module comes from outside,
// so the walk is bounded in depth and in the size of the expanded function.
namespace {

constexpr unsigned maxCallDepth = 128;
constexpr uint64_t maxInlinedInstructions = 1u << 20;

struct CallGraphWalk {
	LLVMContextRef context;
	// functions on the current path (true) and finished ones (false)
	std::unordered_map<LLVMValueRef, bool> onPath;
	// the instruction count of a finished function with everything it calls expanded
	std::unordered_map<LLVMValueRef, uint64_t> expandedSize;
	std::vector<std::pair<LLVMValueRef, LLVMValueRef>> inlinedCalls; // call, callee

	static std::string nameOf(LLVMValueRef function) {
		size_t length = 0;
		auto name = DynamicLLVM::LLVMGetValueName2(function, &length);
		return name ? std::string(name, length) : std::string();
	}

	uint64_t visit(LLVMValueRef function, unsigned depth) {
		if (depth > maxCallDepth) {
			throw std::runtime_error("Calls are nested too deeply (function " + nameOf(function) + ")");
		}
		onPath[function] = true;
		uint64_t size = 0;
		for (auto block = DynamicLLVM::LLVMGetFirstBasicBlock(function); block; block = DynamicLLVM::LLVMGetNextBasicBlock(block)) {
			for (auto inst = DynamicLLVM::LLVMGetFirstInstruction(block); inst; inst = DynamicLLVM::LLVMGetNextInstruction(inst)) {
				++size;
				auto opcode = DynamicLLVM::LLVMGetInstructionOpcode(inst);
				if (opcode == LLVMInvoke || opcode == LLVMCallBr) {
					throw std::runtime_error("Unsupported call instruction in function " + nameOf(function));
				}
				if (opcode != LLVMCall) {
					continue;
				}
				auto callee = DynamicLLVM::LLVMGetCalledValue(inst);
				if (!callee || DynamicLLVM::LLVMGetValueKind(callee) != LLVMFunctionValueKind) {
					throw std::runtime_error("Indirect calls are not supported (in function " + nameOf(function) + ")");
				}
				auto calleeName = nameOf(callee);
				if (DynamicLLVM::LLVMIsDeclaration(callee)) {
					// intrinsics are translated one by one later; anything else has nothing to inline
					if (calleeName.compare(0, 4, "air.") != 0 && calleeName.compare(0, 5, "llvm.") != 0) {
						throw std::runtime_error("Call to function " + calleeName + ", which has no body");
					}
					continue;
				}
				auto calleeType = DynamicLLVM::LLVMGlobalGetValueType(callee);
				if (DynamicLLVM::LLVMGetCalledFunctionType(inst) != calleeType || DynamicLLVM::LLVMIsFunctionVarArg(calleeType)) {
					throw std::runtime_error("Call to function " + calleeName + " does not match its definition");
				}
				auto state = onPath.find(callee);
				if (state != onPath.end() && state->second) {
					throw std::runtime_error("Recursive call of function " + calleeName);
				}
				uint64_t calleeSize;
				if (state == onPath.end()) {
					calleeSize = visit(callee, depth + 1);
				} else {
					calleeSize = expandedSize.at(callee);
				}
				size += calleeSize;
				if (size > maxInlinedInstructions) {
					throw std::runtime_error("The function is too large once its calls are inlined");
				}
				inlinedCalls.emplace_back(inst, callee);
			}
		}
		onPath[function] = false;
		expandedSize[function] = size;
		return size;
	}
};

// Checks the calls reachable from the kernel and marks the called functions so that the always-inline pass
// expands them (including the ones the compiler marked noinline).
void prepareCallsForInlining(LLVMModuleRef module, LLVMValueRef kernel) {
	CallGraphWalk walk;
	walk.context = DynamicLLVM::LLVMGetModuleContext(module);
	walk.visit(kernel, 0);

	if (walk.inlinedCalls.empty()) {
		return;
	}

	auto noinline = DynamicLLVM::LLVMGetEnumAttributeKindForName("noinline", 8);
	auto alwaysinline = DynamicLLVM::LLVMGetEnumAttributeKindForName("alwaysinline", 12);
	auto always = DynamicLLVM::LLVMCreateEnumAttribute(walk.context, alwaysinline, 0);
	for (auto& [call, callee]: walk.inlinedCalls) {
		DynamicLLVM::LLVMRemoveEnumAttributeAtIndex(callee, LLVMAttributeFunctionIndex, noinline);
		DynamicLLVM::LLVMAddAttributeAtIndex(callee, LLVMAttributeFunctionIndex, always);
		DynamicLLVM::LLVMRemoveCallSiteEnumAttribute(call, LLVMAttributeFunctionIndex, noinline);
	}
}

}

Iridium::AIR::Function::Function(Type type, const std::string& name, const void* bitcode, size_t bitcodeSize):
	_name(name),
	_type(type)
{
	if (bitcode == nullptr || bitcodeSize < 16) {
		throw std::runtime_error("Bitcode buffer too small");
	}
	const uint8_t* bc = reinterpret_cast<const uint8_t*>(bitcode);
	bool has_bc_magic = (bc[0] == 'B' && bc[1] == 'C' && bc[2] == 0xc0 && bc[3] == 0xde);
	bool has_wrapper_magic = (bc[0] == 0xde && bc[1] == 0xc0 && bc[2] == 0x17 && bc[3] == 0x0b);
	if (!has_bc_magic && !has_wrapper_magic) {
		throw std::runtime_error("Invalid bitcode magic header");
	}

	LLVMModuleRef moduleRef;
	_bitcodeBuffer = LLVMSupport::MemoryBuffer(DynamicLLVM::LLVMCreateMemoryBufferWithMemoryRange(reinterpret_cast<const char*>(bitcode), bitcodeSize, "", false), DynamicLLVM::LLVMDisposeMemoryBuffer);

	if (!_bitcodeBuffer) {
		throw std::runtime_error("Failed to create LLVM memory buffer");
	}

	if (DynamicLLVM::LLVMParseBitcode2(_bitcodeBuffer.get(), &moduleRef)) {
		throw std::runtime_error("Failed to parse LLVM bitcode");
	}

	_module = LLVMSupport::Module(moduleRef, DynamicLLVM::LLVMDisposeModule);

	// Helper functions called by the kernel are expanded into it first (see prepareCallsForInlining).
	{
		auto kernel = DynamicLLVM::LLVMGetNamedFunction(moduleRef, _name.c_str());
		if (kernel) {
			prepareCallsForInlining(moduleRef, kernel);
		}
	}

	// SPIR-V requires structured control flow, while AIR may contain any reducible CFG and keeps local
	// aggregates in memory. Promote locals to SSA values and give every selection and loop its own merge
	// block before translating. LLVM has no AIR target, so present the module as the SPIR-V it is being
	// lowered to while the passes run, and restore its triple afterwards.
	std::string triple = DynamicLLVM::LLVMGetTarget(moduleRef);
	DynamicLLVM::LLVMSetTarget(moduleRef, "spirv64-unknown-unknown");
	auto passOptions = DynamicLLVM::LLVMCreatePassBuilderOptions();
	auto passError = DynamicLLVM::LLVMRunPasses(moduleRef, "always-inline,function(sroa,mem2reg,loop-unroll,sroa,mem2reg,simplifycfg,structurizecfg)", nullptr, passOptions);
	DynamicLLVM::LLVMDisposePassBuilderOptions(passOptions);
	DynamicLLVM::LLVMSetTarget(moduleRef, triple.c_str());

	if (passError) {
		auto message = DynamicLLVM::LLVMGetErrorMessage(passError);
		std::string detail = message ? message : "unknown LLVM error";
		DynamicLLVM::LLVMDisposeErrorMessage(message);
		throw std::runtime_error("Failed to normalize AIR control flow: " + detail);
	}

	_function = DynamicLLVM::LLVMGetNamedFunction(_module.get(), _name.c_str());

	if (!_function) {
		throw std::runtime_error("Function not found in LLVM module: " + _name);
	}
};

// TEST
#include <iostream>
#include <cctype>
#include <optional>
#include <string>

#if LLVM_VERSION_MAJOR >= 23
	#define IRIDIUM_IS_BR(opcode) ((opcode) == LLVMUncondBr || (opcode) == LLVMCondBr)
#else
	#define IRIDIUM_IS_BR(opcode) ((opcode) == LLVMBr)
#endif

// Thrown when AIR uses something the translation does not implement. The
// message names the unsupported construct, so a failure identifies itself
// instead of surfacing as a bare "std::exception".
class ImpossibleResultID: public std::exception {
	std::string _message;

public:
	explicit ImpossibleResultID(std::string message):
		_message(std::move(message)) {}

	const char* what() const noexcept override { return _message.c_str(); }
};

// While set, structure and array types are declared without explicit layout, as Private and Function
// memory requires.
static thread_local bool g_layoutFreeTypes = false;

// Objects in these storage classes are single variables of their declared type, not memory at an address: they
// are reached through access chains and cannot be reinterpreted.
static bool isLogicalStorageClass(Iridium::SPIRV::StorageClass storageClass) {
	using Iridium::SPIRV::StorageClass;
	return storageClass == StorageClass::Function || storageClass == StorageClass::Private || storageClass == StorageClass::Workgroup;
}

class LayoutFreeScope {
public:
	explicit LayoutFreeScope(bool enabled): _previous(g_layoutFreeTypes) { g_layoutFreeTypes = enabled; }
	~LayoutFreeScope() { g_layoutFreeTypes = _previous; }

private:
	bool _previous;
};

// The distance between consecutive objects of a type: its size rounded up to its
// alignment (12-byte float3 objects sit 16 bytes apart).
static size_t allocationSize(const Iridium::SPIRV::Type& type) {
	return (type.size + type.alignment - 1) & ~(type.alignment - 1);
}

// The type of every pointer into device or constant memory: buffer arguments, getelementptr results and addresses
// loaded from memory. Nothing reads the pointee: a getelementptr computes its byte offset from its own source element
// type, and loads, stores and atomics re-type the pointer to what they access. So a buffer whose element type the
// module doesn't declare (a struct named only in air.arg_type_name) needs no type, and an address loaded from memory
// gets the same type as the argument it may have come from.
static Iridium::SPIRV::ResultID physicalPointerType(Iridium::SPIRV::Builder& builder) {
	using namespace Iridium::SPIRV;
	auto word = builder.declareType(Type(Type::IntegerTag {}, 32, false));
	return builder.declareType(Type(Type::PointerTag {}, StorageClass::PhysicalStorageBuffer, word, 8));
}

// Vulkan requires an access through a physical pointer to be aligned to the largest scalar it reads or writes, which
// for an address is all of its 8 bytes. A smaller alignment in the module is refused rather than assumed to be wrong;
// a larger one is reported as 8.
static unsigned addressAlignment(unsigned alignment) {
	if (alignment < 8) {
		throw ImpossibleResultID("device memory access of an address with alignment " + std::to_string(alignment) + "; addresses need 8");
	}
	return 8;
}

// `physical` is set for a type laid out in device or constant memory, where a device or constant pointer is an address
// and becomes a physicalPointerType. Elsewhere a bare pointer type can't be lowered: it could name a variable, a
// texture or a sampler (textures are addrspace(1) and samplers addrspace(2) as well).
static Iridium::SPIRV::ResultID llvmTypeToSPIRVType(Iridium::SPIRV::Builder& builder, LLVMTypeRef llvmType, bool physical = false) {
	using namespace Iridium::SPIRV;

	auto kind = DynamicLLVM::LLVMGetTypeKind(llvmType);

	switch (kind) {
		case LLVMVoidTypeKind:
			return builder.declareType(Type(Type::VoidTag {}));

		case LLVMHalfTypeKind:
			return builder.declareType(Type(Type::FloatTag {}, 16));

		case LLVMFloatTypeKind:
			return builder.declareType(Type(Type::FloatTag {}, 32));

		case LLVMDoubleTypeKind:
			return builder.declareType(Type(Type::FloatTag {}, 64));

		case LLVMX86_FP80TypeKind:
			return builder.declareType(Type(Type::FloatTag {}, 80));

		case LLVMFP128TypeKind:
			return builder.declareType(Type(Type::FloatTag {}, 128));

		case LLVMIntegerTypeKind:
			if (DynamicLLVM::LLVMGetIntTypeWidth(llvmType) == 1) {
				return builder.declareType(Type(Type::BooleanTag {}));
			}
			return builder.declareType(Type(Type::IntegerTag {}, DynamicLLVM::LLVMGetIntTypeWidth(llvmType), /* TODO: how to determine this? */ true));

		case LLVMFunctionTypeKind: {
			auto retType = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetReturnType(llvmType));
			std::vector<ResultID> paramTypes;
			std::vector<LLVMTypeRef> llvmParamTypes(DynamicLLVM::LLVMCountParamTypes(llvmType));
			DynamicLLVM::LLVMGetParamTypes(llvmType, llvmParamTypes.data());
			for (auto& typeRef: llvmParamTypes) {
				paramTypes.push_back(llvmTypeToSPIRVType(builder, typeRef));
			}
			return builder.declareType(Type(Type::FunctionTag {}, retType, paramTypes, 8));
		} break;

		case LLVMStructTypeKind: {
			std::vector<Type::Member> members;
			auto len = DynamicLLVM::LLVMCountStructElementTypes(llvmType);
			// unfortunately, LLVM's C API provides no way to determine the offset of a structure member.
			// nor does it provide a way to determine the size or alignment of a type as a constant expression.
			// therefore, we'll need to calculate these values ourselves.
			auto structAlignment = 1;
			if (!DynamicLLVM::LLVMIsPackedStruct(llvmType)) {
				// see which member has the greatest alignment
				for (size_t i = 0; i < len; ++i) {
					auto typeAtIndex = DynamicLLVM::LLVMStructGetTypeAtIndex(llvmType, i);
					auto type = llvmTypeToSPIRVType(builder, typeAtIndex, physical);
					auto typeInst = *builder.reverseLookupType(type);
					if (typeInst.alignment > structAlignment) {
						structAlignment = typeInst.alignment;
					}
				}

				// round the alignment up to a vec4 alignment multiple
				// (commented-out because this is wrong when using std430)
				//structAlignment = (structAlignment + 15) & ~15;
			}
			size_t offset = 0;
			for (size_t i = 0; i < len; ++i) {
				auto typeAtIndex = DynamicLLVM::LLVMStructGetTypeAtIndex(llvmType, i);
				auto type = llvmTypeToSPIRVType(builder, typeAtIndex, physical);
				auto typeInst = *builder.reverseLookupType(type);
				auto alignmentToUse = (typeInst.alignment < structAlignment) ? typeInst.alignment : structAlignment;
				// align the offset
				offset = (offset + (alignmentToUse - 1)) & ~(alignmentToUse - 1);
				members.push_back(Type::Member { type, offset, {} });
				// increment the offset
				offset += allocationSize(typeInst);
			}
			// align the final offset to determine the structure size
			offset = (offset + (structAlignment - 1)) & ~(structAlignment - 1);
			Type structType(Type::StructureTag {}, members, offset, structAlignment);
			structType.layoutFree = g_layoutFreeTypes;
			return builder.declareType(structType);
		} break;

		case LLVMPointerTypeKind: {
			auto addrSpace = DynamicLLVM::LLVMGetPointerAddressSpace(llvmType);
			if (physical && (addrSpace == 1 || addrSpace == 2)) {
				return physicalPointerType(builder);
			}
			throw ImpossibleResultID("cannot resolve pointee of opaque pointer (addrspace "
				+ std::to_string((unsigned)addrSpace) + "); derive it from AIR argument metadata");
		} break;

		case LLVMVectorTypeKind: {
			auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetElementType(llvmType));
			return builder.declareType(Type(Type::VectorTag {}, DynamicLLVM::LLVMGetVectorSize(llvmType), type));
		} break;

		case LLVMArrayTypeKind: {
			auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetElementType(llvmType), physical);
			auto typeInst = *builder.reverseLookupType(type);
			auto elmCount = DynamicLLVM::LLVMGetArrayLength(llvmType);
			size_t stride = (typeInst.size + typeInst.alignment - 1) & ~(typeInst.alignment - 1);
			Type arrayType(Type::ArrayTag {}, type, elmCount, stride * elmCount, typeInst.alignment);
			arrayType.layoutFree = g_layoutFreeTypes;
			return builder.declareType(arrayType);
		} break;

		default:
			throw ImpossibleResultID("unhandled LLVM type kind " + std::to_string((int)kind));
	}
};

static std::string_view llvmMDStringToStringView(LLVMValueRef llvmMDString) {
	unsigned int length = 0;
	auto rawStr = DynamicLLVM::LLVMGetMDString(llvmMDString, &length);
	return std::string_view(rawStr, length);
};

// The operands of an AIR argument descriptor: index, kind, then the kind's own entries. A parameter that
// is only present when a function constant holds is described with the wrapper `air.function_constant,
// <predicate>` between the index and the kind; the argument is still declared, so it is described as
// the argument it would be.
static std::vector<LLVMValueRef> airParameterOperands(LLVMValueRef parameterOperand) {
	std::vector<LLVMValueRef> info(DynamicLLVM::LLVMGetMDNodeNumOperands(parameterOperand));
	DynamicLLVM::LLVMGetMDNodeOperands(parameterOperand, info.data());
	if (info.size() >= 4 && info[1] && DynamicLLVM::LLVMIsAMDString(info[1]) && llvmMDStringToStringView(info[1]) == "air.function_constant") {
		info.erase(info.begin() + 1, info.begin() + 3);
	}
	return info;
}

// The kind of a parameter (air.buffer, air.texture, ...): the second operand of its description, which a malformed
// module may not have.
static std::string_view airParameterKind(const std::vector<LLVMValueRef>& parameterInfo) {
	if (parameterInfo.size() < 2 || !parameterInfo[1] || !DynamicLLVM::LLVMIsAMDString(parameterInfo[1])) {
		throw ImpossibleResultID("a parameter description has no kind");
	}
	return llvmMDStringToStringView(parameterInfo[1]);
}

// The integer operand at `index` of a parameter description, which a malformed module may not have.
static int64_t airParameterInteger(const std::vector<LLVMValueRef>& parameterInfo, size_t index) {
	if (index >= parameterInfo.size() || !parameterInfo[index] || !DynamicLLVM::LLVMIsAConstantInt(parameterInfo[index])) {
		throw ImpossibleResultID("a parameter description has a missing or non-integer operand");
	}
	return DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[index]);
}

// The string operand at `index` of a parameter description, which a malformed module may not have.
static std::string_view airParameterString(const std::vector<LLVMValueRef>& parameterInfo, size_t index) {
	if (index >= parameterInfo.size() || !parameterInfo[index] || !DynamicLLVM::LLVMIsAMDString(parameterInfo[index])) {
		throw ImpossibleResultID("a parameter description has a missing or non-string operand");
	}
	return llvmMDStringToStringView(parameterInfo[index]);
}

// Returns the operand that follows the given MDString key in an AIR argument
// descriptor, or nullptr if the key is absent.
static LLVMValueRef findAIMDValue(LLVMValueRef mdNode, std::string_view key) {
	unsigned int count = DynamicLLVM::LLVMGetMDNodeNumOperands(mdNode);
	std::vector<LLVMValueRef> operands(count);
	DynamicLLVM::LLVMGetMDNodeOperands(mdNode, operands.data());

	for (unsigned int i = 0; i + 1 < count; i++) {
		unsigned int length = 0;
		if (!DynamicLLVM::LLVMGetMDString(operands[i], &length)) {
			continue;
		}
		if (llvmMDStringToStringView(operands[i]) == key) {
			return operands[i + 1];
		}
	}

	return nullptr;
}

// Splits a Metal type name into its scalar base name and component count.
// Metal spells vectors by suffixing the scalar with a count ("float4", "uint2")
// and marks the padded vector layouts with a "packed_" prefix, which does not
// change the element type.
static bool splitMetalTypeName(std::string_view name, std::string_view& base, size_t& count, size_t* rows = nullptr) {
	count = 1;
	if (rows) *rows = 1;
	constexpr std::string_view packedPrefix = "packed_";
	bool packed = name.substr(0, packedPrefix.size()) == packedPrefix;
	if (packed) name.remove_prefix(packedPrefix.size());
	auto digits = name.find_first_of("0123456789");
	if (digits == std::string_view::npos) { base = name; return true; }
	base = name.substr(0, digits);
	auto dimensions = name.substr(digits);
	auto dimension = [](char c) { return c >= '2' && c <= '4'; };
	if (dimensions.size() == 1 && dimension(dimensions[0])) {
		count = dimensions[0] - '0';
		return true;
	}
	if (!packed && dimensions.size() == 3 && dimension(dimensions[0]) &&
		dimensions[1] == 'x' && dimension(dimensions[2])) {
		count = dimensions[0] - '0';
		if (rows) *rows = dimensions[2] - '0';
		return true;
	}
	count = 0;
	return false;
}

// Returns std::nullopt for a name that is not a Metal scalar, so callers can
// fall back to resolving it as a named module type.
//
// Integer signedness is deliberately not taken from the Metal name. LLVM IR
// integers carry no signedness, so a type derived from IR (a getelementptr's
// source element type, say) is always signed here. Deriving unsigned types
// from air.arg_type_name would make the two paths disagree: spirv-val rejects
// the mismatch because OpTypeInt 32 0 and OpTypeInt 32 1 are distinct types
// even though the bytes are identical. Metal only gives unsignedness meaning
// at arithmetic sites, so storage and access-chain types are uniformly signed
// to keep one type per layout.
static std::optional<Iridium::SPIRV::Type> spirvScalarTypeForMetalBase(std::string_view base) {
	using Iridium::SPIRV::Type;

	if (base == "float") { return Type(Type::FloatTag {}, 32); }
	if (base == "half") { return Type(Type::FloatTag {}, 16); }
	if (base == "double") { return Type(Type::FloatTag {}, 64); }

	if (base == "int" || base == "uint") { return Type(Type::IntegerTag {}, 32, true); }
	if (base == "short" || base == "ushort") { return Type(Type::IntegerTag {}, 16, true); }
	if (base == "long" || base == "ulong") { return Type(Type::IntegerTag {}, 64, true); }
	if (base == "char" || base == "bool") { return Type(Type::IntegerTag {}, 8, true); }


	if (base == "uchar") { return Type(Type::IntegerTag {}, 8, true); }

	return std::nullopt;
}

// air.convert.f.<to>.f.<from> converts between floating-point types of different widths,
// where each type is a scalar (f16, f32) or a vector of them (v4f16, ...). MSL has no double
// type, so f64 names are rejected rather than lowered to Float64 code.
static bool isFloatWidthConversion(std::string_view name) {
	constexpr std::string_view prefix = "air.convert.";
	if (name.substr(0, prefix.size()) != prefix) {
		return false;
	}

	std::string_view parts[4];
	size_t count = 0;
	std::string_view rest = name.substr(prefix.size());
	while (true) {
		if (count == 4) {
			return false;
		}
		auto dot = rest.find('.');
		parts[count++] = rest.substr(0, dot);
		if (dot == std::string_view::npos) {
			break;
		}
		rest.remove_prefix(dot + 1);
	}

	// Metal vector types have 2, 3 or 4 components (MSL specification, vector data types).
	auto element = [](std::string_view type, std::string_view& scalar, std::string_view& vector) {
		vector = {};
		if (!type.empty() && type[0] == 'v') {
			if (type.size() < 2 || type[1] < '2' || type[1] > '4') {
				return false;
			}
			vector = type.substr(0, 2);
			type.remove_prefix(2);
		}
		scalar = type;
		return scalar == "f16" || scalar == "f32";
	};

	std::string_view toScalar, toVector, fromScalar, fromVector;
	return count == 4 && parts[0] == "f" && parts[2] == "f"
		&& element(parts[1], toScalar, toVector) && element(parts[3], fromScalar, fromVector)
		&& toVector == fromVector && toScalar != fromScalar;
}

// An argument's air.arg_type_name is a scalar, a vector, or the name of a
// struct declared in the module ("Uniforms", "AAPLVertex"). Struct names are
// resolved against the module, since the opaque pointer parameter no longer
// carries the type. LLVM prefixes named struct types with "struct.".
static Iridium::SPIRV::ResultID spirvTypeForAIRTypeName(Iridium::SPIRV::Builder& builder, LLVMModuleRef module, std::string_view name) {
	using Iridium::SPIRV::Type;

	std::string_view base;
	size_t count = 0, rows = 1;
	bool valid = splitMetalTypeName(name, base, count, &rows);

	if (auto scalar = spirvScalarTypeForMetalBase(base)) {
		if (!valid) throw std::runtime_error("Invalid Metal type dimensions: " + std::string(name));
		if (rows > 1 && base != "float" && base != "half")
			throw std::runtime_error("Unsupported Metal matrix element type: " + std::string(name));
		auto scalarID = builder.declareType(*scalar);

		if (count == 1 && rows == 1) {
			return scalarID;
		}

		if (rows > 1) {
			auto column = builder.declareType(Type(Type::VectorTag {}, rows, scalarID));
			auto columnInst = *builder.reverseLookupType(column);
			return builder.declareType(Type(Type::ArrayTag {}, column, count, allocationSize(columnInst) * count, columnInst.alignment));
		}
		// packed_T names the same vector as T
		return builder.declareType(Type(Type::VectorTag {}, count, scalarID));
	}

	std::string owned(name);
	auto named = DynamicLLVM::LLVMGetTypeByName(module, owned.c_str());
	if (!named) {
		named = DynamicLLVM::LLVMGetTypeByName(module, ("struct." + owned).c_str());
	}
	if (named) {
		return llvmTypeToSPIRVType(builder, named);
	}

	throw ImpossibleResultID("neither a Metal scalar nor a module type named \"" + std::string(name) + "\"");
}

static Iridium::SPIRV::ResultID llvmGEPToResultID(Iridium::SPIRV::Builder& builder, LLVMValueRef inst);

// The interface location of a vertex output, fragment input or fragment output, from its AIR descriptor.
// [[user(locnN)]] varyings name their location explicitly (it is what links the two stages), and a
// fragment output carries its color attachment index after air.render_target. Anything else keeps the
// position-based `fallback`.
static uint32_t airInterfaceLocation(LLVMValueRef descriptor, uint32_t fallback) {
	std::vector<LLVMValueRef> operands(DynamicLLVM::LLVMGetMDNodeNumOperands(descriptor));
	DynamicLLVM::LLVMGetMDNodeOperands(descriptor, operands.data());

	for (size_t i = 0; i < operands.size(); ++i) {
		if (!operands[i] || !DynamicLLVM::LLVMIsAMDString(operands[i])) {
			continue;
		}
		auto value = llvmMDStringToStringView(operands[i]);

		constexpr std::string_view userPrefix = "user(locn";
		if (value.size() > userPrefix.size() + 1 && value.substr(0, userPrefix.size()) == userPrefix && value.back() == ')') {
			auto digits = value.substr(userPrefix.size(), value.size() - userPrefix.size() - 1);
			uint32_t location = 0;
			for (char digit: digits) {
				if (digit < '0' || digit > '9') {
					throw ImpossibleResultID("invalid varying location " + std::string(value));
				}
				location = location * 10 + (digit - '0');
			}
			return location;
		}

		if (value == "air.render_target" && i + 1 < operands.size() && DynamicLLVM::LLVMIsAConstantInt(operands[i + 1])) {
			return DynamicLLVM::LLVMConstIntGetZExtValue(operands[i + 1]);
		}
	}

	return fallback;
}

static Iridium::SPIRV::ResultID llvmValueToResultID(Iridium::SPIRV::Builder& builder, LLVMValueRef llvmValue) {
	using namespace Iridium::SPIRV;

	auto kind = DynamicLLVM::LLVMGetValueKind(llvmValue);
	auto type = DynamicLLVM::LLVMTypeOf(llvmValue);
	auto typeKind = DynamicLLVM::LLVMGetTypeKind(type);

	{
		ResultID maybe = builder.lookupResultID(reinterpret_cast<uintptr_t>(llvmValue));
		if (maybe != ResultIDInvalid) {
			if (kind == LLVMGlobalVariableValueKind) {
				// every function using a global variable lists it in its entry point interface
				builder.referenceGlobalVariable(maybe);
			}
			return maybe;
		}
	}

	// LLVM 23 represents a floating-point vector splat (e.g. `<3 x float> zeroinitializer`) as a vector-typed
	// ConstantFP rather than ConstantAggregateZero/ConstantDataVector. Declare the scalar and splat it into a composite.
	if (kind == LLVMConstantFPValueKind && typeKind == LLVMVectorTypeKind) {
		auto elementKind = DynamicLLVM::LLVMGetTypeKind(DynamicLLVM::LLVMGetElementType(type));
		LLVMBool losesInfo = false;
		auto val = DynamicLLVM::LLVMConstRealGetDouble(llvmValue, &losesInfo);
		ResultID element = ResultIDInvalid;
		switch (elementKind) {
			case LLVMFloatTypeKind: element = builder.declareConstantScalar<float>(val); break;
			case LLVMDoubleTypeKind: element = builder.declareConstantScalar<double>(val); break;
			case LLVMHalfTypeKind: {
				Iridium::Float16 tmp = val;
				element = builder.declareConstantScalar<const Iridium::Float16*>(&tmp);
			} break;
			default:
				throw ImpossibleResultID("unsupported float vector constant element type kind " + std::to_string((int)elementKind));
		}
		std::vector<ResultID> vals(DynamicLLVM::LLVMGetVectorSize(type), element);
		return builder.declareConstantComposite(llvmTypeToSPIRVType(builder, type), vals);
	}

	switch (kind) {
		case LLVMGlobalVariableValueKind: {
			// a threadgroup variable (address space 3), shared by the threads of a threadgroup
			if (DynamicLLVM::LLVMGetPointerAddressSpace(type) == 3) {
				LayoutFreeScope layoutFree(true);
				auto valueType = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGlobalGetValueType(llvmValue));
				auto pointerType = builder.declareType(Type(Type::PointerTag {}, StorageClass::Workgroup, valueType, 8));
				auto result = builder.addGlobalVariable(pointerType, StorageClass::Workgroup);
				builder.setResultType(result, pointerType);
				builder.associateExistingResultID(result, reinterpret_cast<uintptr_t>(llvmValue));
				builder.referenceGlobalVariable(result);
				return result;
			}

			// a constant the function keeps in memory, such as a lookup table
			auto initializer = DynamicLLVM::LLVMGetInitializer(llvmValue);
			if (!initializer) {
				throw ImpossibleResultID("global variable without an initializer");
			}
			LayoutFreeScope layoutFree(true);
			auto valueType = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGlobalGetValueType(llvmValue));
			auto pointerType = builder.declareType(Type(Type::PointerTag {}, StorageClass::Private, valueType, 8));
			// an undefined initial value leaves the variable uninitialized: OpUndef is not a constant instruction,
			// so it cannot be a variable's initializer
			auto initializerKind = DynamicLLVM::LLVMGetValueKind(initializer);
			bool undefinedInitializer = initializerKind == LLVMUndefValueValueKind || initializerKind == LLVMPoisonValueValueKind;
			auto initialValue = undefinedInitializer ? ResultIDInvalid : llvmValueToResultID(builder, initializer);
			auto result = builder.addGlobalVariable(pointerType, StorageClass::Private, initialValue);
			builder.setResultType(result, pointerType);
			builder.associateExistingResultID(result, reinterpret_cast<uintptr_t>(llvmValue));
			builder.referenceGlobalVariable(result);
			return result;
		} break;

		case LLVMConstantIntValueKind: {
			auto width = DynamicLLVM::LLVMGetIntTypeWidth(type);
			if (width != 1 && width != 8 && width != 16 && width != 32 && width != 64) {
				throw ImpossibleResultID("unsupported integer constant width " + std::to_string(width));
			}
			auto val = DynamicLLVM::LLVMConstIntGetSExtValue(llvmValue);
			switch (width) {
				case 1: return builder.declareConstantScalar<bool>(val != 0);
				case 8: return builder.declareConstantScalar<int8_t>(val);
				case 16: return builder.declareConstantScalar<int16_t>(val);
				case 32: return builder.declareConstantScalar<int32_t>(val);
				case 64: return builder.declareConstantScalar<int64_t>(val);
			}
			throw ImpossibleResultID("unreachable integer constant width");
		} break;

		case LLVMConstantFPValueKind: {
			LLVMBool losesInfo = false;
			auto val = DynamicLLVM::LLVMConstRealGetDouble(llvmValue, &losesInfo);

			switch (typeKind) {
				case LLVMFloatTypeKind:
					return builder.declareConstantScalar<float>(val);

				case LLVMDoubleTypeKind:
					return builder.declareConstantScalar<double>(val);

				case LLVMHalfTypeKind: {
					Iridium::Float16 tmp = val;
					return builder.declareConstantScalar<const Iridium::Float16*>(&tmp);
				} break;

				default:
					throw ImpossibleResultID("unsupported float constant type kind " + std::to_string((int)typeKind));
			}
		} break;

		case LLVMConstantStructValueKind:
		case LLVMConstantArrayValueKind:
		case LLVMConstantVectorValueKind: {
			std::vector<ResultID> vals;
			auto count = typeKind == LLVMStructTypeKind ? DynamicLLVM::LLVMCountStructElementTypes(type)
				: (typeKind == LLVMArrayTypeKind ? DynamicLLVM::LLVMGetArrayLength(type) : DynamicLLVM::LLVMGetVectorSize(type));

			for (size_t i = 0; i < count; ++i) {
				auto constant = DynamicLLVM::LLVMGetOperand(llvmValue, i);
				vals.push_back(llvmValueToResultID(builder, constant));
			}

			return builder.declareConstantComposite(llvmTypeToSPIRVType(builder, type), vals);
		} break;

		case LLVMConstantDataArrayValueKind:
		case LLVMConstantDataVectorValueKind: {
			std::vector<ResultID> vals;

			auto count = typeKind == LLVMArrayTypeKind ? DynamicLLVM::LLVMGetArrayLength(type) : DynamicLLVM::LLVMGetVectorSize(type);

			for (size_t i = 0; i < count; ++i) {
				auto constant = DynamicLLVM::LLVMGetElementAsConstant(llvmValue, i);
				vals.push_back(llvmValueToResultID(builder, constant));
			}

			return builder.declareConstantComposite(llvmTypeToSPIRVType(builder, type), vals);
		} break;

		case LLVMPoisonValueValueKind:
		case LLVMUndefValueValueKind: {
			return builder.declareUndefinedValue(llvmTypeToSPIRVType(builder, type));
		} break;

		case LLVMConstantAggregateZeroValueKind: {
			// SPIR-V's concept of 'null' seems to map pretty cleanly to LLVM's zero-initializers
			return builder.declareNullValue(llvmTypeToSPIRVType(builder, type));
		} break;

		case LLVMConstantExprValueKind: {
			switch (DynamicLLVM::LLVMGetConstOpcode(llvmValue)) {
				case LLVMGetElementPtr: {
					auto base = llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(llvmValue, 0));
					auto pointer = builder.reverseLookupType(builder.lookupResultType(base));
					if (!pointer || pointer->backingType != Type::BackingType::Pointer ||
						(pointer->pointerStorageClass != StorageClass::PhysicalStorageBuffer && !isLogicalStorageClass(pointer->pointerStorageClass)))
					{
						throw ImpossibleResultID("constant getelementptr requires a known physical buffer pointer or a global variable");
					}
					return llvmGEPToResultID(builder, llvmValue);
				} break;

				case LLVMBitCast: {
					auto target = DynamicLLVM::LLVMGetOperand(llvmValue, 0);
					auto targetID = llvmValueToResultID(builder, target);
					auto targetType = builder.lookupResultType(targetID);
					auto targetTypeInst = *builder.reverseLookupType(targetType);

#if 1
					if (targetTypeInst.backingType == Type::BackingType::Pointer) {
						auto targetElmTypeInst = *builder.reverseLookupType(targetTypeInst.targetType);

						if (targetElmTypeInst.backingType == Type::BackingType::Sampler) {
							// HACK: don't bitcast sampler pointers. while this should be perfectly legal SPIR-V (and should be allowed in the Vulkan environment),
							//       some vendors (e.g. AMD) don't handle this properly and it can cause segfaults. we don't really need to do anything weird with it anyways
							//       (i'm 95% sure Metal bitcode doesn't do any pointer shenanigans with samplers), so just keep the original pointer.
							return targetID;
						}
					}
#endif

					return builder.encodeBitcast(llvmTypeToSPIRVType(builder, type), targetID);
				} break;

				default:
					throw ImpossibleResultID("unsupported LLVM value kind " + std::to_string((int)kind));
			}
		} break;

		default:
			throw ImpossibleResultID("unsupported LLVM value kind " + std::to_string((int)kind));
	}
};

static bool isThreeLaneVector(LLVMTypeRef type) {
	return DynamicLLVM::LLVMGetTypeKind(type) == LLVMVectorTypeKind && DynamicLLVM::LLVMGetVectorSize(type) == 3;
}

// The first 3-lane vector in a type, looking through array elements and struct members.
static LLVMTypeRef threeLaneVectorIn(LLVMTypeRef type) {
	if (isThreeLaneVector(type)) {
		return type;
	}
	switch (DynamicLLVM::LLVMGetTypeKind(type)) {
		case LLVMArrayTypeKind:
			return threeLaneVectorIn(DynamicLLVM::LLVMGetElementType(type));
		case LLVMStructTypeKind:
			for (unsigned i = 0; i < DynamicLLVM::LLVMCountStructElementTypes(type); ++i) {
				if (auto vector = threeLaneVectorIn(DynamicLLVM::LLVMStructGetTypeAtIndex(type, i))) {
					return vector;
				}
			}
			return nullptr;
		default:
			return nullptr;
	}
}

static Iridium::SPIRV::ResultID llvmGEPToResultID(Iridium::SPIRV::Builder& builder, LLVMValueRef inst) {
	using namespace Iridium;
	auto base = DynamicLLVM::LLVMGetOperand(inst, 0);

	// With opaque pointers LLVMTypeOf(inst) is just the
	// pointer, so the result pointee is recovered from the
	// instruction's source element type: the first index
	// steps the pointer itself, and each later index
	// descends one level into an aggregate.
	auto currentType = DynamicLLVM::LLVMGetGEPSourceElementType(inst);
	if (!currentType) {
		throw ImpossibleResultID("getelementptr has no source element type");
	}

	std::vector<SPIRV::ResultID> indices;
	auto operandCount = DynamicLLVM::LLVMGetNumOperands(inst);
	if (operandCount < 2) {
		throw ImpossibleResultID("getelementptr without indices");
	}

	for (size_t i = 1; i < operandCount; ++i) {
		auto llindex = DynamicLLVM::LLVMGetOperand(inst, i);
		if (i == 1) {
			auto indexType = DynamicLLVM::LLVMTypeOf(llindex);
			if (DynamicLLVM::LLVMGetTypeKind(indexType) != LLVMIntegerTypeKind) {
				throw ImpossibleResultID("getelementptr first index is not an integer");
			}
			auto width = DynamicLLVM::LLVMGetIntTypeWidth(indexType);
			if (width != 8 && width != 16 && width != 32 && width != 64) {
				throw ImpossibleResultID("unsupported getelementptr first index width");
			}
			auto index = llvmValueToResultID(builder, llindex);
			if (width < 64) {
				auto signed64Type = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 64, true));
				index = builder.encodeArithUnop(SPIRV::Opcode::SConvert, signed64Type, index);
				builder.setResultType(index, signed64Type);
			}
			indices.push_back(index);
		} else {
			indices.push_back(llvmValueToResultID(builder, llindex));
		}

		if (i > 1) {
			switch (DynamicLLVM::LLVMGetTypeKind(currentType)) {
				case LLVMArrayTypeKind:
				case LLVMVectorTypeKind:
					currentType = DynamicLLVM::LLVMGetElementType(currentType);
					break;

				case LLVMStructTypeKind: {
					auto indexValue = DynamicLLVM::LLVMIsAConstantInt(llindex)
						? DynamicLLVM::LLVMConstIntGetZExtValue(llindex)
						: 0;
					auto fieldType = DynamicLLVM::LLVMStructGetTypeAtIndex(currentType, indexValue);
					if (!fieldType) {
						throw ImpossibleResultID("getelementptr into a struct with a non-constant index");
					}
					currentType = fieldType;
				} break;

				default:
					throw ImpossibleResultID("getelementptr descends into a non-aggregate type");
			}
		}
	}

	auto tmp2 = llvmValueToResultID(builder, base);
	auto origTypeID = builder.lookupResultType(tmp2);
	auto maybeOrigType = builder.reverseLookupType(origTypeID);
	if (!maybeOrigType || maybeOrigType->backingType != SPIRV::Type::BackingType::Pointer) {
		throw ImpossibleResultID("getelementptr base has no recorded pointer type");
	}
	auto origType = *maybeOrigType;
	bool logical = isLogicalStorageClass(origType.pointerStorageClass);
	LayoutFreeScope layoutFree(logical);

	bool physical = origType.pointerStorageClass == SPIRV::StorageClass::PhysicalStorageBuffer;

	// ensure the resulting pointer storage class is the same as the input pointer storage class
	auto resultType = physical
		? physicalPointerType(builder)
		: builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, origType.pointerStorageClass, llvmTypeToSPIRVType(builder, currentType), 8));

	auto uint64Type = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 64, false));

	SPIRV::ResultID resID = SPIRV::ResultIDInvalid;

	if (physical) {
		// A physical pointer carries no layout to index through, and the
		// instruction's source element type need not be the pointee the
		// pointer was declared with (a buffer of float4x4 can be addressed
		// as floats or float4s). Compute the byte offset from the source
		// element type's own layout.
		auto sourceType = *builder.reverseLookupType(llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetGEPSourceElementType(inst), true));
		auto offset = builder.encodeArithBinop(SPIRV::Opcode::IMul, uint64Type, indices[0], builder.declareConstantScalar<uint64_t>(allocationSize(sourceType)));
		auto layoutType = DynamicLLVM::LLVMGetGEPSourceElementType(inst);

		for (size_t i = 2; i < operandCount; ++i) {
			auto llindex = DynamicLLVM::LLVMGetOperand(inst, i);
			auto layout = *builder.reverseLookupType(llvmTypeToSPIRVType(builder, layoutType, true));
			SPIRV::ResultID step = SPIRV::ResultIDInvalid;

			switch (DynamicLLVM::LLVMGetTypeKind(layoutType)) {
				case LLVMStructTypeKind: {
					if (!DynamicLLVM::LLVMIsAConstantInt(llindex)) {
						throw ImpossibleResultID("getelementptr into a struct with a non-constant index");
					}
					auto member = DynamicLLVM::LLVMConstIntGetZExtValue(llindex);
					step = builder.declareConstantScalar<uint64_t>(layout.structureMembers.at(member).offset);
					layoutType = DynamicLLVM::LLVMStructGetTypeAtIndex(layoutType, member);
				} break;

				case LLVMArrayTypeKind:
				case LLVMVectorTypeKind: {
					auto elementType = DynamicLLVM::LLVMGetElementType(layoutType);
					auto element = *builder.reverseLookupType(llvmTypeToSPIRVType(builder, elementType, true));
					auto stride = DynamicLLVM::LLVMGetTypeKind(layoutType) == LLVMArrayTypeKind
						? allocationSize(element)
						: element.size;
					auto index = indices[i - 1];
					auto indexWidth = DynamicLLVM::LLVMGetIntTypeWidth(DynamicLLVM::LLVMTypeOf(llindex));
					if (indexWidth < 64) {
						auto signed64Type = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 64, true));
						index = builder.encodeArithUnop(SPIRV::Opcode::SConvert, signed64Type, index);
						builder.setResultType(index, signed64Type);
					}
					step = builder.encodeArithBinop(SPIRV::Opcode::IMul, uint64Type, index, builder.declareConstantScalar<uint64_t>(stride));
					layoutType = elementType;
				} break;

				default:
					throw ImpossibleResultID("getelementptr descends into a non-aggregate type");
			}

			offset = builder.encodeArithBinop(SPIRV::Opcode::IAdd, uint64Type, offset, step);
		}

		auto asInteger = builder.encodeConvertPtrToU(uint64Type, tmp2);
		auto added = builder.encodeArithBinop(SPIRV::Opcode::IAdd, uint64Type, asInteger, offset);
		resID = builder.encodeConvertUToPtr(resultType, added);
	} else if (logical) {
		// A local variable or global constant has no address to offset, but it is a single object of
		// the source element type, so a getelementptr into it always starts with a zero index and
		// becomes an access chain through the remaining ones.
		auto first = DynamicLLVM::LLVMGetOperand(inst, 1);
		if (!DynamicLLVM::LLVMIsAConstantInt(first) || DynamicLLVM::LLVMConstIntGetZExtValue(first) != 0) {
			throw ImpossibleResultID("getelementptr steps past a local or constant object");
		}
		if (llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetGEPSourceElementType(inst)) != origType.targetType) {
			throw ImpossibleResultID("getelementptr reinterprets a local or constant object");
		}
		indices.erase(indices.begin());
		resID = indices.empty() ? tmp2 : builder.encodeAccessChain(resultType, tmp2, indices);
	} else {
		// Every buffer argument is a physical pointer; the other storage classes
		// (images, samplers, stage I/O) have no byte address to offset.
		throw ImpossibleResultID("getelementptr on a non-physical pointer (storage class "
			+ std::to_string(static_cast<uint32_t>(origType.pointerStorageClass)) + ") is unsupported");
	}

	builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
	builder.setResultType(resID, resultType);
	return resID;
}

// An opaque-pointer load or store may name the first scalar or element of an aggregate variable through the
// variable itself (offset zero). Steps the pointer into the first array element or structure member until
// it points at `wantedType` (the first scalar when that is ResultIDInvalid), or leaves it untouched when that is not
// reachable.
static Iridium::SPIRV::ResultID descendToFirstElement(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID pointer, Iridium::SPIRV::ResultID wantedType) {
	using namespace Iridium::SPIRV;
	auto pointerType = *builder.reverseLookupType(builder.lookupResultType(pointer));
	auto current = pointerType.targetType;
	std::vector<uint32_t> indices;
	while (current != wantedType) {
		auto type = *builder.reverseLookupType(current);
		if (wantedType == ResultIDInvalid && type.backingType != Type::BackingType::Array && type.backingType != Type::BackingType::Structure) {
			// no particular type wanted: stop at the first scalar
			wantedType = current;
			break;
		}
		if (type.backingType == Type::BackingType::Array) {
			current = type.targetType;
		} else if (type.backingType == Type::BackingType::Structure && !type.structureMembers.empty()) {
			current = type.structureMembers.at(0).id;
		} else {
			return pointer;
		}
		indices.push_back(builder.declareConstantScalar<int32_t>(0));
	}
	if (indices.empty()) {
		return pointer;
	}
	auto resultType = builder.declareType(Type(Type::PointerTag {}, pointerType.pointerStorageClass, wantedType, 8));
	auto access = builder.encodeAccessChain(resultType, pointer, indices);
	builder.setResultType(access, resultType);
	return access;
}

// Translates the AIR atomic functions of 32-bit and 64-bit integers and of 32-bit floats,
// air.atomic.<global|local>.<operation>[.<s|u>].<i32|i64|f32>, whose arguments are the pointer, the value (not for a load), the memory order, the scope and a volatile flag. Returns
// false when `name` is not one of them. The order is relaxed unless the module asks for more. The functions that update
// memory return the value that was there before, except the 64-bit minimum and maximum, which return nothing.
static bool translateAirAtomic(Iridium::SPIRV::Builder& builder, LLVMValueRef inst, std::string_view name, Iridium::SPIRV::ResultID resultType, Iridium::SPIRV::ResultID& resID) {
	using namespace Iridium::SPIRV;
	constexpr std::string_view prefix = "air.atomic.";
	if (name.substr(0, prefix.size()) != prefix) {
		return false;
	}

	// <global|local>.<operation>[.<s|u>].i32
	std::vector<std::string_view> parts;
	for (auto rest = name.substr(prefix.size()); !rest.empty();) {
		auto dot = rest.find('.');
		parts.push_back(rest.substr(0, dot));
		rest = dot == rest.npos ? std::string_view() : rest.substr(dot + 1);
	}
	if (parts.size() < 3 || parts.size() > 4 || (parts.back() != "i32" && parts.back() != "i64" && parts.back() != "f32") || (parts[0] != "global" && parts[0] != "local")) {
		throw ImpossibleResultID("unsupported atomic function " + std::string(name));
	}
	unsigned width = parts.back() == "i64" ? 64 : 32;
	bool isFloat = parts.back() == "f32";
	if (width == 64) {
		builder.requireCapability(Capability::Int64);
		builder.requireCapability(Capability::Int64Atomics);
	}
	auto operation = parts[1];
	bool isSigned = parts.size() == 4 && parts[2] == "s";
	bool store = operation == "store";
	bool load = operation == "load";
	bool compareExchange = operation == "cmpxchg";

	// the last operand of a call is the callee
	unsigned arguments = DynamicLLVM::LLVMGetNumOperands(inst) - 1;
	// compare-and-exchange takes the address of the expected value too, and an order for each outcome
	unsigned valueArguments = load ? 0 : (compareExchange ? 2 : 1);
	if (arguments < 3 + valueArguments + (compareExchange ? 1 : 0)) {
		throw ImpossibleResultID("atomic function " + std::string(name) + " has too few arguments");
	}
	auto constantArgument = [&](unsigned i) -> uint64_t {
		auto value = DynamicLLVM::LLVMGetOperand(inst, i);
		if (!DynamicLLVM::LLVMIsAConstantInt(value)) {
			throw ImpossibleResultID("atomic function " + std::string(name) + " with a non-constant order or scope");
		}
		return DynamicLLVM::LLVMConstIntGetZExtValue(value);
	};
	uint32_t order = constantArgument(1 + valueArguments);
	uint32_t failureOrder = compareExchange ? constantArgument(2 + valueArguments) : 0;
	// the scope the module names (1 for threadgroup memory, 2 for device memory) decides the SPIR-V scope
	uint32_t scope = parts[0] == "local" ? 2 : 1;
	// SPIR-V memory semantics: relaxed is none, anything stronger acquire-release (Metal only has relaxed)
	uint32_t semantics = order == 0 ? 0 : 0x8;

	auto pointer = llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 0));
	auto pointerInfo = builder.reverseLookupType(builder.lookupResultType(pointer));
	if (!pointerInfo || pointerInfo->backingType != Type::BackingType::Pointer) {
		throw ImpossibleResultID("atomic function on something that is not a pointer");
	}
	auto storageClass = pointerInfo->pointerStorageClass;
	auto integerType = isFloat ? builder.declareType(Type(Type::FloatTag {}, 32)) : builder.declareType(Type(Type::IntegerTag {}, width, true));
	if (isLogicalStorageClass(storageClass)) {
		pointer = descendToFirstElement(builder, pointer, ResultIDInvalid);
	} else if (pointerInfo->targetType != integerType) {
		// a physical pointer: view the memory as the integer the function works on
		auto integerPointer = builder.declareType(Type(Type::PointerTag {}, storageClass, integerType, 8));
		auto casted = builder.encodeBitcast(integerPointer, pointer);
		builder.setResultType(casted, integerPointer);
		pointer = casted;
	}
	// a threadgroup variable keeps the type it was declared with; values are cast to it
	auto memoryType = builder.reverseLookupType(builder.lookupResultType(pointer))->targetType;
	auto memoryInfo = *builder.reverseLookupType(memoryType);
	if (memoryInfo.backingType != (isFloat ? Type::BackingType::FloatingPoint : Type::BackingType::Integer) || memoryInfo.scalarWidth != width) {
		throw ImpossibleResultID("atomic function on memory that is not a " + std::to_string(width) + (isFloat ? "-bit float" : "-bit integer"));
	}
	auto asMemoryType = [&](ResultID value) {
		return builder.lookupResultType(value) == memoryType ? value : builder.encodeBitcast(memoryType, value);
	};

	auto scopeID = builder.declareConstantScalar<uint32_t>(scope);
	auto semanticsID = builder.declareConstantScalar<uint32_t>(semantics);

	if (load) {
		auto value = builder.encodeOp(Opcode::AtomicLoad, memoryType, { pointer, scopeID, semanticsID });
		resID = builder.lookupResultType(value) == resultType ? value : builder.encodeBitcast(resultType, value);
		return true;
	}

	if (compareExchange) {
		if (isFloat) {
			throw ImpossibleResultID("compare-and-exchange of floats is unsupported");
		}
		// (pointer, pointer to the expected value, desired value, success order, failure order, ...): the value found in
		// memory is also written to the expected value (on a success it equals it already)
		auto expectedPointer = llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 1));
		auto expectedInfo = builder.reverseLookupType(builder.lookupResultType(expectedPointer));
		if (!expectedInfo || expectedInfo->backingType != Type::BackingType::Pointer || !isLogicalStorageClass(expectedInfo->pointerStorageClass)) {
			throw ImpossibleResultID("compare-and-exchange with an expected value that is not a local variable");
		}
		expectedPointer = descendToFirstElement(builder, expectedPointer, ResultIDInvalid);
		auto expectedType = builder.reverseLookupType(builder.lookupResultType(expectedPointer))->targetType;
		auto expected = asMemoryType(builder.encodeLoad(expectedType, expectedPointer));
		auto desired = asMemoryType(llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 2)));
		uint32_t failureSemantics = failureOrder == 0 ? 0 : 0x8;
		auto old = builder.encodeOp(Opcode::AtomicCompareExchange, memoryType, { pointer, scopeID, semanticsID, builder.declareConstantScalar<uint32_t>(failureSemantics), desired, expected });
		builder.encodeStore(expectedPointer, builder.lookupResultType(old) == expectedType ? old : builder.encodeBitcast(expectedType, old));
		// the call returns the value that was in memory; the module compares it with the expected value itself
		resID = builder.lookupResultType(old) == resultType ? old : builder.encodeBitcast(resultType, old);
		return true;
	}

	auto value = asMemoryType(llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 1)));
	if (store) {
		builder.encodeOpWithoutResult(Opcode::AtomicStore, { pointer, scopeID, semanticsID, value });
		return true;
	}

	Opcode opcode;
	if (isFloat && operation != "xchg" && operation != "add" && operation != "sub") {
		throw ImpossibleResultID("unsupported atomic function " + std::string(name));
	}
	if (operation == "xchg") {
		opcode = Opcode::AtomicExchange;
	} else if (isFloat) {
		// there is no float subtraction: add the negated value
		builder.requireExtension("SPV_EXT_shader_atomic_float_add");
		builder.requireCapability(Capability::AtomicFloat32AddEXT);
		if (operation == "sub") {
			value = builder.encodeOp(Opcode::FNegate, memoryType, { value });
		}
		opcode = Opcode::AtomicFAddEXT;
	} else if (operation == "add") {
		opcode = Opcode::AtomicIAdd;
	} else if (operation == "sub") {
		opcode = Opcode::AtomicISub;
	} else if (operation == "min") {
		opcode = isSigned ? Opcode::AtomicSMin : Opcode::AtomicUMin;
	} else if (operation == "max") {
		opcode = isSigned ? Opcode::AtomicSMax : Opcode::AtomicUMax;
	} else if (operation == "and") {
		opcode = Opcode::AtomicAnd;
	} else if (operation == "or") {
		opcode = Opcode::AtomicOr;
	} else if (operation == "xor") {
		opcode = Opcode::AtomicXor;
	} else {
		throw ImpossibleResultID("unsupported atomic function " + std::string(name));
	}
	auto old = builder.encodeOp(opcode, memoryType, { pointer, scopeID, semanticsID, value });
	// a call that returns nothing has no use for the old value
	if (DynamicLLVM::LLVMGetTypeKind(DynamicLLVM::LLVMTypeOf(inst)) != LLVMVoidTypeKind) {
		resID = builder.lookupResultType(old) == resultType ? old : builder.encodeBitcast(resultType, old);
	}
	return true;
}

static Iridium::SPIRV::ResultID integerTypeWithSign(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID typeID, bool isSigned);

// Translates the AIR simdgroup functions, air.simd_<operation>[.<s|u>].<i32|f32>, which work on the threads of a
// subgroup that execute together: reductions, prefix scans, broadcasts, shuffles, votes and ballots (the 64-bit mask
// of the active threads or of those whose predicate holds). Returns false when `name` is not one of them. Only 32-bit
// integers and floats are supported.
static bool translateAirSimd(Iridium::SPIRV::Builder& builder, LLVMValueRef inst, std::string_view name, Iridium::SPIRV::ResultID resultType, Iridium::SPIRV::ResultID& resID) {
	using namespace Iridium::SPIRV;
	constexpr std::string_view prefix = "air.simd_";
	if (name.substr(0, prefix.size()) != prefix) {
		return false;
	}

	// <operation>[.<s|u>].<type>
	std::vector<std::string_view> parts;
	for (auto rest = name.substr(prefix.size()); !rest.empty();) {
		auto dot = rest.find('.');
		parts.push_back(rest.substr(0, dot));
		rest = dot == rest.npos ? std::string_view() : rest.substr(dot + 1);
	}
	if (parts.empty() || parts.size() > 3) {
		throw ImpossibleResultID("unsupported simdgroup function " + std::string(name));
	}
	auto operation = parts[0];
	bool isSigned = parts.size() == 3 && parts[1] == "s";
	auto typeName = parts.size() > 1 ? parts.back() : std::string_view();
	bool isFloat = typeName == "f32";

	// the last operand of a call is the callee
	unsigned arguments = DynamicLLVM::LLVMGetNumOperands(inst) - 1;
	auto argument = [&](unsigned i) {
		if (i >= arguments) {
			throw ImpossibleResultID("simdgroup function " + std::string(name) + " has too few arguments");
		}
		return llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, i));
	};
	auto scopeID = builder.declareConstantScalar<uint32_t>(3);
	auto u32Type = builder.declareType(Type(Type::IntegerTag {}, 32, false));
	// the value operand in the type the call returns (integers have no sign in LLVM, and SPIR-V wants both the same)
	auto typed = [&](unsigned i) {
		auto id = argument(i);
		return builder.lookupResultType(id) == resultType ? id : builder.encodeBitcast(resultType, id);
	};
	// the lane operand is a 16-bit integer
	auto lane = [&](unsigned i) {
		auto id = argument(i);
		auto type = builder.reverseLookupType(builder.lookupResultType(id));
		return type && type->backingType == Type::BackingType::Integer && type->scalarWidth == 32 ? id : builder.encodeUConvert(u32Type, id);
	};

	if (operation == "all" || operation == "any") {
		builder.requireCapability(Capability::GroupNonUniformVote);
		resID = builder.encodeOp(operation == "all" ? Opcode::GroupNonUniformAll : Opcode::GroupNonUniformAny, resultType, { scopeID, argument(0) });
		return true;
	}

	if (operation == "ballot" || operation == "active_threads_mask") {
		builder.requireCapability(Capability::GroupNonUniformBallot);
		auto predicate = operation == "ballot" ? argument(0) : builder.declareConstantScalar<bool>(true);
		auto words = builder.declareType(Type(Type::VectorTag {}, 4, u32Type));
		auto ballot = builder.encodeOp(Opcode::GroupNonUniformBallot, words, { scopeID, predicate });
		// the first two words are the 64 lanes the mask holds
		auto low = builder.encodeUConvert(resultType, builder.encodeCompositeExtract(u32Type, ballot, { 0 }));
		auto high = builder.encodeUConvert(resultType, builder.encodeCompositeExtract(u32Type, ballot, { 1 }));
		high = builder.encodeArithBinop(Opcode::ShiftLeftLogical, resultType, high, builder.declareConstantScalar<uint32_t>(32));
		resID = builder.encodeArithBinop(Opcode::BitwiseOr, resultType, low, high);
		return true;
	}

	if (typeName != "i32" && typeName != "f32") {
		throw ImpossibleResultID("unsupported simdgroup function " + std::string(name));
	}

	// reductions and scans: the operation, and the group operation (reduce 0, inclusive scan 1, exclusive scan 2)
	uint32_t groupOperation = 0;
	std::string_view arithmetic = operation;
	if (operation.substr(0, 17) == "prefix_inclusive_") {
		groupOperation = 1;
		arithmetic = operation.substr(17);
	} else if (operation.substr(0, 17) == "prefix_exclusive_") {
		groupOperation = 2;
		arithmetic = operation.substr(17);
	}
	Opcode opcode;
	bool known = true;
	if (arithmetic == "sum") {
		opcode = isFloat ? Opcode::GroupNonUniformFAdd : Opcode::GroupNonUniformIAdd;
	} else if (arithmetic == "product") {
		opcode = isFloat ? Opcode::GroupNonUniformFMul : Opcode::GroupNonUniformIMul;
	} else if (arithmetic == "min") {
		opcode = isFloat ? Opcode::GroupNonUniformFMin : (isSigned ? Opcode::GroupNonUniformSMin : Opcode::GroupNonUniformUMin);
	} else if (arithmetic == "max") {
		opcode = isFloat ? Opcode::GroupNonUniformFMax : (isSigned ? Opcode::GroupNonUniformSMax : Opcode::GroupNonUniformUMax);
	} else if (!isFloat && arithmetic == "and") {
		opcode = Opcode::GroupNonUniformBitwiseAnd;
	} else if (!isFloat && arithmetic == "or") {
		opcode = Opcode::GroupNonUniformBitwiseOr;
	} else if (!isFloat && arithmetic == "xor") {
		opcode = Opcode::GroupNonUniformBitwiseXor;
	} else {
		known = false;
		opcode = Opcode::GroupNonUniformIAdd;
	}
	if (known && (groupOperation == 0 || arithmetic == "sum" || arithmetic == "product")) {
		builder.requireCapability(Capability::GroupNonUniformArithmetic);
		// the unsigned minimum and maximum work on unsigned integers
		if (opcode == Opcode::GroupNonUniformUMin || opcode == Opcode::GroupNonUniformUMax) {
			auto unsignedType = integerTypeWithSign(builder, resultType, false);
			auto value = builder.encodeBitcast(unsignedType, typed(0));
			resID = builder.encodeBitcast(resultType, builder.encodeOp(opcode, unsignedType, { scopeID, groupOperation, value }));
			return true;
		}
		resID = builder.encodeOp(opcode, resultType, { scopeID, groupOperation, typed(0) });
		return true;
	}

	// broadcasts and shuffles of the first operand
	if (operation == "broadcast_first") {
		builder.requireCapability(Capability::GroupNonUniformBallot);
		resID = builder.encodeOp(Opcode::GroupNonUniformBroadcastFirst, resultType, { scopeID, typed(0) });
		return true;
	}
	if (operation == "broadcast") {
		auto value = typed(0);
		auto laneValue = DynamicLLVM::LLVMGetOperand(inst, 1);
		if (arguments > 1 && DynamicLLVM::LLVMIsAConstantInt(laneValue)) {
			builder.requireCapability(Capability::GroupNonUniformBallot);
			resID = builder.encodeOp(Opcode::GroupNonUniformBroadcast, resultType, { scopeID, value, builder.declareConstantScalar<uint32_t>(static_cast<uint32_t>(DynamicLLVM::LLVMConstIntGetZExtValue(laneValue))) });
		} else {
			builder.requireCapability(Capability::GroupNonUniformShuffle);
			resID = builder.encodeOp(Opcode::GroupNonUniformShuffle, resultType, { scopeID, value, lane(1) });
		}
		return true;
	}
	bool shuffle = operation == "shuffle";
	bool xorShuffle = operation == "shuffle_xor";
	bool down = operation == "shuffle_down";
	bool up = operation == "shuffle_up";
	if (shuffle || xorShuffle || down || up) {
		builder.requireCapability(down || up ? Capability::GroupNonUniformShuffleRelative : Capability::GroupNonUniformShuffle);
		auto opcodeForShuffle = shuffle ? Opcode::GroupNonUniformShuffle : (xorShuffle ? Opcode::GroupNonUniformShuffleXor : (down ? Opcode::GroupNonUniformShuffleDown : Opcode::GroupNonUniformShuffleUp));
		resID = builder.encodeOp(opcodeForShuffle, resultType, { scopeID, typed(0), lane(1) });
		return true;
	}

	throw ImpossibleResultID("unsupported simdgroup function " + std::string(name));
}

// The same integer type, scalar or vector, with the given signedness. LLVM integers have no sign, while
// some SPIR-V instructions require their operands or result to be unsigned.
static Iridium::SPIRV::ResultID integerTypeWithSign(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID typeID, bool isSigned) {
	using namespace Iridium::SPIRV;
	auto type = *builder.reverseLookupType(typeID);
	if (type.backingType == Type::BackingType::Vector) {
		return builder.declareType(Type(Type::VectorTag {}, type.entryCount, integerTypeWithSign(builder, type.targetType, isSigned)));
	}
	if (type.backingType != Type::BackingType::Integer) {
		throw ImpossibleResultID("expected an integer type");
	}
	return builder.declareType(Type(Type::IntegerTag {}, type.scalarWidth, isSigned));
}

// An integer constant of the given scalar or vector integer type (every lane holds `value`).
static Iridium::SPIRV::ResultID integerConstantOfType(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID typeID, int64_t value) {
	using namespace Iridium::SPIRV;
	auto type = *builder.reverseLookupType(typeID);
	if (type.backingType == Type::BackingType::Vector) {
		std::vector<ResultID> lanes(type.entryCount, integerConstantOfType(builder, type.targetType, value));
		return builder.declareConstantComposite(typeID, lanes);
	}
	// the constant has the signedness of the type: an OpSelect or OpPhi needs operands of its result type
	bool isSigned = type.integerIsSigned;
	switch (type.scalarWidth) {
		case 8: return isSigned ? builder.declareConstantScalar<int8_t>(value) : builder.declareConstantScalar<uint8_t>(value);
		case 16: return isSigned ? builder.declareConstantScalar<int16_t>(value) : builder.declareConstantScalar<uint16_t>(value);
		case 32: return isSigned ? builder.declareConstantScalar<int32_t>(value) : builder.declareConstantScalar<uint32_t>(value);
		case 64: return isSigned ? builder.declareConstantScalar<int64_t>(value) : builder.declareConstantScalar<uint64_t>(value);
		default: throw ImpossibleResultID("unsupported integer width " + std::to_string(type.scalarWidth));
	}
}

// A floating-point constant of the given scalar or vector float type.
static Iridium::SPIRV::ResultID floatConstantOfType(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID typeID, double value) {
	using namespace Iridium::SPIRV;
	auto type = *builder.reverseLookupType(typeID);
	if (type.backingType == Type::BackingType::Vector) {
		std::vector<ResultID> lanes(type.entryCount, floatConstantOfType(builder, type.targetType, value));
		return builder.declareConstantComposite(typeID, lanes);
	}
	switch (type.scalarWidth) {
		case 16: {
			Iridium::Float16 half = value;
			return builder.declareConstantScalar<const Iridium::Float16*>(&half);
		}
		case 32: return builder.declareConstantScalar<float>(value);
		case 64: return builder.declareConstantScalar<double>(value);
		default: throw ImpossibleResultID("unsupported float width " + std::to_string(type.scalarWidth));
	}
}

// Emits `opcode` with an unsigned result type, as SPIR-V requires for UConvert, UDiv, UMod and
// ConvertFToU, and reinterprets the result as `typeID`.
static Iridium::SPIRV::ResultID encodeUnsignedResultOp(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::Opcode opcode, Iridium::SPIRV::ResultID typeID, const std::vector<uint32_t>& operands) {
	auto unsignedType = integerTypeWithSign(builder, typeID, false);
	auto result = builder.encodeOp(opcode, unsignedType, operands);
	return unsignedType == typeID ? result : builder.encodeBitcast(typeID, result);
}

static std::vector<std::string_view> splitAirName(std::string_view name) {
	std::vector<std::string_view> parts;
	while (true) {
		auto dot = name.find('.');
		parts.push_back(name.substr(0, dot));
		if (dot == std::string_view::npos) {
			break;
		}
		name.remove_prefix(dot + 1);
	}
	return parts;
}

// "f32", "f16", "v3f32", ...
static bool airTypeTokenIsFloat(std::string_view token) {
	if (!token.empty() && token[0] == 'v') {
		token.remove_prefix(1);
		while (!token.empty() && token[0] >= '0' && token[0] <= '9') {
			token.remove_prefix(1);
		}
	}
	return !token.empty() && token[0] == 'f';
}

// Translates the AIR math, relational, conversion and derivative intrinsics that map directly onto a
// SPIR-V or GLSL.std.450 instruction. Returns false when `name` is not one of them.
static bool translateAirIntrinsic(Iridium::SPIRV::Builder& builder, LLVMValueRef inst, std::string_view name, Iridium::SPIRV::ResultID type, Iridium::SPIRV::ResultID& resID) {
	using namespace Iridium::SPIRV;

	auto operand = [&](unsigned index) {
		return llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, index));
	};

	auto parts = splitAirName(name);
	if (parts.size() < 3 || parts[0] != "air") {
		return false;
	}

	std::string_view fn = parts[1];
	if (fn.substr(0, 5) == "fast_") {
		fn.remove_prefix(5);
	} else if (fn.substr(0, 8) == "precise_") {
		fn.remove_prefix(8);
	}

	// floating-point functions: air.<fn>.<type>
	if (parts.size() == 3 && airTypeTokenIsFloat(parts[2])) {
		struct MathOp {
			std::string_view name;
			GLSLOpcode opcode;
			unsigned arity;
		};
		static const MathOp mathOps[] = {
			{ "sqrt", GLSLOpcode::Sqrt, 1 }, { "rsqrt", GLSLOpcode::InverseSqrt, 1 }, { "pow", GLSLOpcode::Pow, 2 },
			{ "powr", GLSLOpcode::Pow, 2 }, { "exp", GLSLOpcode::Exp, 1 }, { "exp2", GLSLOpcode::Exp2, 1 },
			{ "log", GLSLOpcode::Log, 1 }, { "log2", GLSLOpcode::Log2, 1 }, { "sin", GLSLOpcode::Sin, 1 },
			{ "cos", GLSLOpcode::Cos, 1 }, { "tan", GLSLOpcode::Tan, 1 }, { "asin", GLSLOpcode::Asin, 1 },
			{ "acos", GLSLOpcode::Acos, 1 }, { "atan", GLSLOpcode::Atan, 1 }, { "atan2", GLSLOpcode::Atan2, 2 },
			{ "sinh", GLSLOpcode::Sinh, 1 }, { "cosh", GLSLOpcode::Cosh, 1 }, { "tanh", GLSLOpcode::Tanh, 1 },
			{ "fabs", GLSLOpcode::FAbs, 1 }, { "floor", GLSLOpcode::Floor, 1 }, { "ceil", GLSLOpcode::Ceil, 1 },
			{ "fract", GLSLOpcode::Fract, 1 }, { "rint", GLSLOpcode::RoundEven, 1 }, { "round", GLSLOpcode::Round, 1 },
			{ "trunc", GLSLOpcode::Trunc, 1 }, { "sign", GLSLOpcode::FSign, 1 }, { "fmin", GLSLOpcode::FMin, 2 },
			{ "fmax", GLSLOpcode::FMax, 2 }, { "fma", GLSLOpcode::Fma, 3 }, { "mix", GLSLOpcode::FMix, 3 },
			{ "clamp", GLSLOpcode::FClamp, 3 }, { "step", GLSLOpcode::Step, 2 }, { "smoothstep", GLSLOpcode::SmoothStep, 3 },
			{ "normalize", GLSLOpcode::Normalize, 1 }, { "length", GLSLOpcode::Length, 1 }, { "distance", GLSLOpcode::Distance, 2 },
			{ "cross", GLSLOpcode::Cross, 2 }, { "reflect", GLSLOpcode::Reflect, 2 },
		};

		for (auto& op: mathOps) {
			if (op.name == fn) {
				std::vector<ResultID> arguments;
				for (unsigned i = 0; i < op.arity; ++i) {
					arguments.push_back(operand(i));
				}
				resID = builder.encodeGLSLOp(op.opcode, type, arguments);
				return true;
			}
		}

		if (fn == "saturate") {
			resID = builder.encodeGLSLOp(GLSLOpcode::FClamp, type, { operand(0), floatConstantOfType(builder, type, 0), floatConstantOfType(builder, type, 1) });
			return true;
		}
		if (fn == "fmod") {
			// MSL's fmod is C's: the result has the sign of x, as OpFRem's does
			resID = builder.encodeOp(Opcode::FRem, type, { operand(0), operand(1) });
			return true;
		}
		if (fn == "dot") {
			resID = builder.encodeArithBinop(Opcode::Dot, type, operand(0), operand(1));
			return true;
		}
		if (fn == "dfdx" || fn == "dfdy" || fn == "fwidth") {
			resID = builder.encodeOp(fn == "dfdx" ? Opcode::DPdx : (fn == "dfdy" ? Opcode::DPdy : Opcode::Fwidth), type, { operand(0) });
			return true;
		}
		return false;
	}

	// boolean vector reductions: air.all.v2i1, air.any.v4i1
	if (parts.size() == 3 && (fn == "all" || fn == "any")) {
		resID = builder.encodeOp(fn == "all" ? Opcode::All : Opcode::Any, type, { operand(0) });
		return true;
	}

	// bit functions: air.<fn>.<type>
	if (parts.size() == 3 && (fn == "popcount" || fn == "clz" || fn == "ctz" || fn == "reverse_bits" || fn == "rotate")) {
		auto info = *builder.reverseLookupType(type);
		bool vector = info.backingType == Type::BackingType::Vector;
		auto scalar = vector ? *builder.reverseLookupType(info.targetType) : info;
		auto unsignedType = integerTypeWithSign(builder, type, false);
		auto fromUnsigned = [&](ResultID value) {
			return unsignedType == type ? value : builder.encodeBitcast(type, value);
		};
		auto argument = operand(0);
		if (fn == "popcount") {
			resID = builder.encodeOp(Opcode::BitCount, type, { argument });
		} else if (fn == "reverse_bits") {
			resID = builder.encodeOp(Opcode::BitReverse, type, { argument });
		} else if (fn == "rotate") {
			// rotate left by n (modulo the width): the bits shifted out come back in on the right
			auto mask = integerConstantOfType(builder, type, scalar.scalarWidth - 1);
			auto width = integerConstantOfType(builder, type, scalar.scalarWidth);
			auto left = builder.encodeArithBinop(Opcode::BitwiseAnd, type, operand(1), mask);
			auto right = builder.encodeArithBinop(Opcode::BitwiseAnd, type, builder.encodeArithBinop(Opcode::ISub, type, width, left), mask);
			auto shifted = builder.encodeArithBinop(Opcode::ShiftLeftLogical, type, argument, left);
			resID = builder.encodeArithBinop(Opcode::BitwiseOr, type, shifted, builder.encodeArithBinop(Opcode::ShiftRightLogical, type, argument, right));
			// a rotation by zero shifts right by the full width, which is undefined: select the input then
			auto boolScalar = builder.declareType(Type(Type::BooleanTag {}));
			auto boolType = vector ? builder.declareType(Type(Type::VectorTag {}, info.entryCount, boolScalar)) : boolScalar;
			auto none = builder.encodeOp(Opcode::IEqual, boolType, { left, integerConstantOfType(builder, type, 0) });
			resID = builder.encodeOp(Opcode::Select, type, { none, argument, resID });
		} else if (fn == "clz") {
			// FindUMsb is the index of the highest set bit, -1 for zero: width - 1 minus that is the number of
			// leading zeros, and the width for zero
			auto highest = builder.encodeGLSLOp(GLSLOpcode::FindUMsb, unsignedType, { builder.encodeBitcast(unsignedType, argument) });
			auto count = builder.encodeArithBinop(Opcode::ISub, unsignedType, integerConstantOfType(builder, unsignedType, scalar.scalarWidth - 1), highest);
			resID = fromUnsigned(count);
		} else {
			// FindILsb is the index of the lowest set bit, -1 for zero, where the number of trailing zeros is the width
			auto lowest = builder.encodeGLSLOp(GLSLOpcode::FindILsb, unsignedType, { builder.encodeBitcast(unsignedType, argument) });
			auto boolScalar = builder.declareType(Type(Type::BooleanTag {}));
			auto boolType = vector ? builder.declareType(Type(Type::VectorTag {}, info.entryCount, boolScalar)) : boolScalar;
			auto isZero = builder.encodeOp(Opcode::IEqual, boolType, { argument, integerConstantOfType(builder, type, 0) });
			resID = fromUnsigned(builder.encodeOp(Opcode::Select, unsignedType, { isZero, integerConstantOfType(builder, unsignedType, scalar.scalarWidth), lowest }));
		}
		return true;
	}

	// integer functions: air.<fn>.<s|u>.<type>
	if (parts.size() == 4 && (parts[2] == "s" || parts[2] == "u")) {
		bool isSigned = parts[2] == "s";

		if (fn == "min" || fn == "max") {
			auto opcode = fn == "min" ? (isSigned ? GLSLOpcode::SMin : GLSLOpcode::UMin) : (isSigned ? GLSLOpcode::SMax : GLSLOpcode::UMax);
			resID = builder.encodeGLSLOp(opcode, type, { operand(0), operand(1) });
			return true;
		}
		if (fn == "clamp") {
			resID = builder.encodeGLSLOp(isSigned ? GLSLOpcode::SClamp : GLSLOpcode::UClamp, type, { operand(0), operand(1), operand(2) });
			return true;
		}
		if (fn == "insert_bits") {
			// (base, insert, offset, count)
			resID = builder.encodeOp(Opcode::BitFieldInsert, type, { operand(0), operand(1), operand(2), operand(3) });
			return true;
		}
		if (fn == "extract_bits") {
			// (value, offset, count)
			resID = builder.encodeOp(isSigned ? Opcode::BitFieldSExtract : Opcode::BitFieldUExtract, type, { operand(0), operand(1), operand(2) });
			return true;
		}
		if (fn == "mul_hi") {
			// the high half of the double-width product: the second member of OpUMulExtended's pair
			// OpUMulExtended works on unsigned integers, and LLVM's integer types carry no sign
			auto info = *builder.reverseLookupType(type);
			auto operandType = isSigned ? type : integerTypeWithSign(builder, type, false);
			auto operandInfo = *builder.reverseLookupType(operandType);
			auto typedOperand = [&](size_t index) { return operandType == type ? operand(index) : builder.encodeBitcast(operandType, operand(index)); };
			LayoutFreeScope layoutFree(true);
			auto pair = builder.declareType(Type(Type::StructureTag {}, { Type::Member { operandType, 0, {} }, Type::Member { operandType, operandInfo.size, {} } }, info.size * 2, info.alignment));
			auto product = builder.encodeOp(isSigned ? Opcode::SMulExtended : Opcode::UMulExtended, pair, { typedOperand(0), typedOperand(1) });
			resID = builder.encodeCompositeExtract(operandType, product, { 1 });
			if (operandType != type) {
				resID = builder.encodeBitcast(type, resID);
			}
			return true;
		}
		if (fn == "abs") {
			// the absolute value of an unsigned number is the number itself
			resID = isSigned ? builder.encodeGLSLOp(GLSLOpcode::SAbs, type, { operand(0) }) : operand(0);
			return true;
		}
		return false;
	}

	// conversions: air.convert.<destination kind>.<destination type>.<source kind>.<source type>
	if (parts.size() == 6 && fn == "convert") {
		auto dstKind = parts[2];
		auto dstType = parts[3];
		auto srcKind = parts[4];
		auto srcType = parts[5];
		auto argument = operand(0);
		auto argumentType = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(DynamicLLVM::LLVMGetOperand(inst, 0)));

		if (srcType == "i1" || (srcType.size() > 2 && srcType[0] == 'v' && srcType.substr(srcType.size() - 2) == "i1")) {
			// a boolean becomes 1 or 0
			auto one = dstKind == "f" ? floatConstantOfType(builder, type, 1) : integerConstantOfType(builder, type, 1);
			auto zero = dstKind == "f" ? floatConstantOfType(builder, type, 0) : integerConstantOfType(builder, type, 0);
			resID = builder.encodeOp(Opcode::Select, type, { argument, one, zero });
			return true;
		}

		if (dstKind == "f" && srcKind == "s") {
			resID = builder.encodeOp(Opcode::ConvertSToF, type, { argument });
		} else if (dstKind == "f" && srcKind == "u") {
			resID = builder.encodeOp(Opcode::ConvertUToF, type, { builder.encodeBitcast(integerTypeWithSign(builder, argumentType, false), argument) });
		} else if (dstKind == "s" && srcKind == "f") {
			resID = builder.encodeOp(Opcode::ConvertFToS, type, { argument });
		} else if (dstKind == "u" && srcKind == "f") {
			resID = encodeUnsignedResultOp(builder, Opcode::ConvertFToU, type, { argument });
		} else if (dstKind == "f" && srcKind == "f") {
			resID = (dstType == srcType) ? argument : builder.encodeFConvert(type, argument);
		} else if ((dstKind == "s" || dstKind == "u") && (srcKind == "s" || srcKind == "u")) {
			if (dstType == srcType) {
				resID = argument;
			} else if (srcKind == "s") {
				resID = builder.encodeOp(Opcode::SConvert, type, { argument });
			} else {
				resID = encodeUnsignedResultOp(builder, Opcode::UConvert, type, { argument });
			}
		} else {
			return false;
		}
		return true;
	}

	// packed integer to float vector: air.unpack.<format>[.srgb].<destination type>, the MSL unpack_*_to_float
	// and unpack_*_to_half functions. The formats are the ones GLSL.std.450 unpacks; the sRGB variant decodes
	// the first three channels from sRGB to linear and leaves alpha alone.
	if (fn == "unpack" && (parts.size() == 4 || parts.size() == 5)) {
		bool srgb = parts.size() == 5;
		if (srgb && parts[3] != "srgb") {
			return false;
		}
		auto format = parts[2];
		GLSLOpcode opcode;
		size_t lanes;
		if (format == "unorm4x8") {
			opcode = GLSLOpcode::UnpackUnorm4x8;
			lanes = 4;
		} else if (format == "snorm4x8" && !srgb) {
			opcode = GLSLOpcode::UnpackSnorm4x8;
			lanes = 4;
		} else if (format == "unorm2x16" && !srgb) {
			opcode = GLSLOpcode::UnpackUnorm2x16;
			lanes = 2;
		} else if (format == "snorm2x16" && !srgb) {
			opcode = GLSLOpcode::UnpackSnorm2x16;
			lanes = 2;
		} else {
			return false;
		}

		auto u32 = builder.declareType(Type(Type::IntegerTag {}, 32, false));
		auto f32 = builder.declareType(Type(Type::FloatTag {}, 32));
		auto vectorType = builder.declareType(Type(Type::VectorTag {}, lanes, f32));
		auto packed = operand(0);
		if (builder.lookupResultType(packed) != u32) {
			packed = builder.encodeBitcast(u32, packed);
		}
		auto unpacked = builder.encodeGLSLOp(opcode, vectorType, { packed });

		if (srgb) {
			auto boolean = builder.declareType(Type(Type::BooleanTag {}));
			auto threshold = builder.declareConstantScalar<float>(0.04045f);
			auto linearDivisor = builder.declareConstantScalar<float>(12.92f);
			auto offset = builder.declareConstantScalar<float>(0.055f);
			auto scale = builder.declareConstantScalar<float>(1.055f);
			auto exponent = builder.declareConstantScalar<float>(2.4f);
			std::vector<uint32_t> channels;
			for (uint32_t channel = 0; channel < 4; ++channel) {
				auto value = builder.encodeCompositeExtract(f32, unpacked, { channel });
				if (channel < 3) {
					auto isLinearSegment = builder.encodeOp(Opcode::FOrdLessThan, boolean, { value, threshold });
					auto linear = builder.encodeArithBinop(Opcode::FDiv, f32, value, linearDivisor);
					auto shifted = builder.encodeArithBinop(Opcode::FDiv, f32, builder.encodeArithBinop(Opcode::FAdd, f32, value, offset), scale);
					value = builder.encodeOp(Opcode::Select, f32, { isLinearSegment, linear, builder.encodePow(f32, shifted, exponent) });
				}
				channels.push_back(value);
			}
			unpacked = builder.encodeOp(Opcode::CompositeConstruct, vectorType, channels);
		}

		resID = type == vectorType ? unpacked : builder.encodeFConvert(type, unpacked);
		return true;
	}

	return false;
}

// Converts a texel from the type an image is accessed with to the type the shader uses: half textures
// are accessed as float ones (see the texture argument declarations), and the sign of integer texels can
// differ.
static Iridium::SPIRV::ResultID convertTexel(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID value, Iridium::SPIRV::ResultID fromType, Iridium::SPIRV::ResultID toType) {
	using namespace Iridium::SPIRV;
	if (fromType == toType) {
		return value;
	}
	auto target = *builder.reverseLookupType(toType);
	auto component = *builder.reverseLookupType(target.backingType == Type::BackingType::Vector ? target.targetType : toType);
	return component.backingType == Type::BackingType::FloatingPoint ? builder.encodeFConvert(toType, value) : builder.encodeBitcast(toType, value);
}

// Reads the components of an integer scalar or vector whose value is fixed when the shader is translated: a
// constant, or insertelement and shufflevector instructions over constants. The compiler builds a texel offset
// that way when it is assembled one component at a time, e.g. from a loop counter that unrolling has replaced
// with constants. Undefined components count as zero. Returns false for a value that is only known at run time.
static bool airConstantIntegers(LLVMValueRef value, std::vector<int64_t>& components, unsigned depth = 0) {
	// AIR comes from outside: give up on chains deeper than any offset needs instead of recursing without bound
	if (depth > 16) {
		return false;
	}
	auto type = DynamicLLVM::LLVMTypeOf(value);
	bool vector = DynamicLLVM::LLVMGetTypeKind(type) == LLVMVectorTypeKind;
	unsigned count = vector ? DynamicLLVM::LLVMGetVectorSize(type) : 1;
	if (DynamicLLVM::LLVMGetTypeKind(vector ? DynamicLLVM::LLVMGetElementType(type) : type) != LLVMIntegerTypeKind) {
		return false;
	}

	auto scalar = [](LLVMValueRef element, int64_t& result) {
		auto kind = DynamicLLVM::LLVMGetValueKind(element);
		if (kind == LLVMUndefValueValueKind || kind == LLVMPoisonValueValueKind) {
			result = 0;
			return true;
		}
		if (kind != LLVMConstantIntValueKind) {
			return false;
		}
		result = DynamicLLVM::LLVMConstIntGetSExtValue(element);
		return true;
	};

	switch (DynamicLLVM::LLVMGetValueKind(value)) {
		case LLVMConstantIntValueKind:
		case LLVMUndefValueValueKind:
		case LLVMPoisonValueValueKind: {
			// (a vector-typed ConstantInt is a splat)
			int64_t element;
			scalar(value, element);
			components.assign(count, element);
			return true;
		}

		case LLVMConstantAggregateZeroValueKind:
			components.assign(count, 0);
			return true;

		case LLVMConstantDataVectorValueKind:
		case LLVMConstantVectorValueKind: {
			bool data = DynamicLLVM::LLVMGetValueKind(value) == LLVMConstantDataVectorValueKind;
			components.resize(count);
			for (unsigned i = 0; i < count; ++i) {
				auto element = data ? DynamicLLVM::LLVMGetElementAsConstant(value, i) : DynamicLLVM::LLVMGetOperand(value, i);
				if (!scalar(element, components[i])) {
					return false;
				}
			}
			return true;
		}

		case LLVMInstructionValueKind:
			break;

		default:
			return false;
	}

	switch (DynamicLLVM::LLVMGetInstructionOpcode(value)) {
		case LLVMInsertElement: {
			auto index = DynamicLLVM::LLVMGetOperand(value, 2);
			int64_t element;
			if (!DynamicLLVM::LLVMIsAConstantInt(index) || !airConstantIntegers(DynamicLLVM::LLVMGetOperand(value, 0), components, depth + 1) || !scalar(DynamicLLVM::LLVMGetOperand(value, 1), element)) {
				return false;
			}
			auto position = DynamicLLVM::LLVMConstIntGetZExtValue(index);
			// an index past the end makes the whole result poison
			if (position >= components.size()) {
				components.assign(count, 0);
			} else {
				components[position] = element;
			}
			return true;
		}

		case LLVMShuffleVector: {
			std::vector<int64_t> first, second;
			if (!airConstantIntegers(DynamicLLVM::LLVMGetOperand(value, 0), first, depth + 1) || !airConstantIntegers(DynamicLLVM::LLVMGetOperand(value, 1), second, depth + 1)) {
				return false;
			}
			unsigned maskCount = DynamicLLVM::LLVMGetNumMaskElements(value);
			int undefinedElement = DynamicLLVM::LLVMGetUndefMaskElem();
			components.assign(maskCount, 0);
			for (unsigned i = 0; i < maskCount; ++i) {
				int selector = DynamicLLVM::LLVMGetMaskValue(value, i);
				if (selector == undefinedElement || selector < 0) {
					continue;
				}
				if (static_cast<size_t>(selector) < first.size()) {
					components[i] = first[selector];
				} else if (static_cast<size_t>(selector) - first.size() < second.size()) {
					components[i] = second[selector - first.size()];
				}
			}
			return true;
		}

		default:
			return false;
	}
}

// A texel offset as an image operand: ConstOffset with a constant when its value is fixed at translation time
// (see airConstantIntegers), otherwise nothing (`constant` is false) and the caller has to apply it itself, since
// Vulkan accepts a run-time Offset operand only on gathers, and only with an optional feature.
static Iridium::SPIRV::ResultID airTexelOffset(Iridium::SPIRV::Builder& builder, LLVMValueRef offset, bool& constant) {
	using namespace Iridium::SPIRV;
	std::vector<int64_t> components;
	constant = airConstantIntegers(offset, components);
	if (!constant) {
		return ResultIDInvalid;
	}
	std::vector<ResultID> elements;
	for (auto component: components) {
		elements.push_back(builder.declareConstantScalar<int32_t>(static_cast<int32_t>(component)));
	}
	if (elements.size() == 1) {
		return elements[0];
	}
	auto int32Type = builder.declareType(Type(Type::IntegerTag {}, 32, true));
	return builder.declareConstantComposite(builder.declareType(Type(Type::VectorTag {}, static_cast<uint32_t>(elements.size()), int32Type)), elements);
}

// Applies a texel offset that is only known at run time to sampling coordinates, by moving the coordinates by
// offset / size of `level` (an integer) in each dimension. The Metal Shading Language applies the offset to the
// texel coordinates before the address mode, as Vulkan does with ConstOffset, so this addresses the same texels
// for normalized coordinates. `coordinates` may end with an array layer, which is kept as it is.
static Iridium::SPIRV::ResultID offsetSampleCoordinates(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID image, unsigned dims, bool arrayed, Iridium::SPIRV::ResultID coordinates, Iridium::SPIRV::ResultID componentType, LLVMValueRef offsetValue, Iridium::SPIRV::ResultID level) {
	using namespace Iridium::SPIRV;
	builder.requireCapability(Capability::ImageQuery);
	auto int32Type = builder.declareType(Type(Type::IntegerTag {}, 32, true));
	unsigned count = dims + (arrayed ? 1 : 0);
	auto coordinatesType = count == 1 ? componentType : builder.declareType(Type(Type::VectorTag {}, count, componentType));

	auto offset = llvmValueToResultID(builder, offsetValue);
	auto offsetLLVMType = DynamicLLVM::LLVMTypeOf(offsetValue);
	bool offsetIsVector = DynamicLLVM::LLVMGetTypeKind(offsetLLVMType) == LLVMVectorTypeKind;
	auto offsetComponentType = llvmTypeToSPIRVType(builder, offsetIsVector ? DynamicLLVM::LLVMGetElementType(offsetLLVMType) : offsetLLVMType);

	auto sizeType = count == 1 ? int32Type : builder.declareType(Type(Type::VectorTag {}, count, int32Type));
	auto size = builder.encodeOp(Opcode::ImageQuerySizeLod, sizeType, { image, level });

	std::vector<uint32_t> components;
	for (unsigned i = 0; i < count; ++i) {
		auto coordinate = count == 1 ? coordinates : builder.encodeCompositeExtract(componentType, coordinates, { i });
		if (i < dims) {
			auto texels = count == 1 ? size : builder.encodeCompositeExtract(int32Type, size, { i });
			auto delta = offsetIsVector ? builder.encodeCompositeExtract(offsetComponentType, offset, { i }) : offset;
			auto step = builder.encodeArithBinop(Opcode::FDiv, componentType,
				builder.encodeOp(Opcode::ConvertSToF, componentType, { delta }),
				builder.encodeOp(Opcode::ConvertSToF, componentType, { texels }));
			coordinate = builder.encodeArithBinop(Opcode::FAdd, componentType, coordinate, step);
		}
		components.push_back(coordinate);
	}
	return count == 1 ? components[0] : builder.encodeOp(Opcode::CompositeConstruct, coordinatesType, components);
}

// The level whose size scales a run-time offset (see offsetSampleCoordinates): the given LOD rounded to the
// nearest level and kept within the image's levels.
static Iridium::SPIRV::ResultID offsetLevel(Iridium::SPIRV::Builder& builder, Iridium::SPIRV::ResultID image, Iridium::SPIRV::ResultID lod, Iridium::SPIRV::ResultID lodType) {
	using namespace Iridium::SPIRV;
	builder.requireCapability(Capability::ImageQuery);
	auto int32Type = builder.declareType(Type(Type::IntegerTag {}, 32, true));
	auto rounded = builder.encodeOp(Opcode::ConvertFToS, int32Type, { builder.encodeGLSLOp(GLSLOpcode::Round, lodType, { lod }) });
	auto levels = builder.encodeOp(Opcode::ImageQueryLevels, int32Type, { image });
	auto last = builder.encodeArithBinop(Opcode::ISub, int32Type, levels, builder.declareConstantScalar<int32_t>(1));
	return builder.encodeGLSLOp(GLSLOpcode::SClamp, int32Type, { rounded, builder.declareConstantScalar<int32_t>(0), last });
}

// Translates the AIR texture intrinsics: sampling (with or without a depth comparison), gathers, reads, writes and size
// queries. Returns false when `name` is not one of them. `samplerFor` resolves a sampler operand (a constant sampler has no value of its own).
template<typename SamplerFor, typename IsReadSampler>
static bool translateAirTexture(Iridium::SPIRV::Builder& builder, LLVMValueRef inst, std::string_view name, Iridium::SPIRV::ResultID resultType, Iridium::FunctionType stage, SamplerFor&& samplerFor, IsReadSampler&& isReadSampler, Iridium::SPIRV::ResultID& resID) {
	using namespace Iridium::SPIRV;

	bool compare = name.substr(0, 25) == "air.sample_compare_depth_";
	bool sample = name.substr(0, 19) == "air.sample_texture_" || compare;
	// the multisample depth texture is read like a color one, with a constant in front of the coordinates
	bool readDepthMultisample = name.substr(0, 21) == "air.read_depth_2d_ms.";
	bool read = name.substr(0, 17) == "air.read_texture_" || name.substr(0, 20) == "air.read_depth_2d_ms";
	bool write = name.substr(0, 18) == "air.write_texture_";
	// the size, sample count and level count of a texture of any class that has them
	auto queryName = [&](std::string_view prefix) {
		return name.substr(0, prefix.size()) == prefix && (name.substr(prefix.size(), 8) == "texture_" || name.substr(prefix.size(), 6) == "depth_");
	};
	bool width = queryName("air.get_width_");
	bool height = queryName("air.get_height_");
	bool depthQuery = queryName("air.get_depth_");
	bool arraySize = queryName("air.get_array_size_");
	bool numSamples = queryName("air.get_num_samples_");
	bool numLevels = queryName("air.get_num_mip_levels_");
	bool query = width || height || depthQuery || arraySize || numSamples || numLevels;
	// gather() of a color texture, gather() of a depth texture and gather_compare()
	bool gatherColor = name.substr(0, 19) == "air.gather_texture_";
	bool gatherDepth = name.substr(0, 17) == "air.gather_depth_";
	bool gatherCompare = name.substr(0, 25) == "air.gather_compare_depth_";
	bool gather = gatherColor || gatherDepth || gatherCompare;
	if (!sample && !read && !write && !query && !gather) {
		return false;
	}

	// the last operand of a call is the callee; AIR comes from outside, so a call with fewer arguments than the
	// intrinsic takes is rejected instead of read past its end
	auto argumentCount = DynamicLLVM::LLVMGetNumOperands(inst) - 1;
	auto argument = [&](unsigned i) {
		if (i >= argumentCount) {
			throw ImpossibleResultID("texture intrinsic " + std::string(name) + " has too few arguments");
		}
		return DynamicLLVM::LLVMGetOperand(inst, i);
	};
	auto operand = [&](unsigned i) {
		return llvmValueToResultID(builder, argument(i));
	};

	auto loadSampler = [&](LLVMValueRef value) {
		auto samplerPointer = samplerFor(value);
		auto samplerType = builder.declareType(Type(Type::SamplerTag {}));
		auto pointerType = builder.reverseLookupType(builder.lookupResultType(samplerPointer));
		if (!pointerType || pointerType->backingType != Type::BackingType::Pointer || pointerType->targetType != samplerType) {
			throw ImpossibleResultID("sampler operand is not a sampler argument");
		}
		return builder.encodeLoad(samplerType, samplerPointer);
	};

	auto texturePointer = operand(0);
	auto pointerType = builder.reverseLookupType(builder.lookupResultType(texturePointer));
	if (!pointerType || pointerType->backingType != Type::BackingType::Pointer
		|| builder.reverseLookupType(pointerType->targetType)->backingType != Type::BackingType::Image) {
		throw ImpossibleResultID("texture operand is not a texture argument");
	}
	auto imageTypeID = pointerType->targetType;
	auto imageType = *builder.reverseLookupType(imageTypeID);
	auto image = builder.encodeLoad(imageTypeID, texturePointer);
	bool texelBuffer = imageType.imageDimensionality == Dim::eBuffer;
	unsigned dims = (imageType.imageDimensionality == Dim::e1D || texelBuffer) ? 1 : (imageType.imageDimensionality == Dim::e2D ? 2 : 3);
	bool arrayed = imageType.imageIsArrayed;
	bool storage = imageType.imageIsSampledIndication == 2;
	auto int32Type = builder.declareType(Type(Type::IntegerTag {}, 32, true));

	if (query) {
		builder.requireCapability(Capability::ImageQuery);
		bool cube = imageType.imageDimensionality == Dim::eCube;
		bool multisampled = imageType.imageIsMultisampled;
		// the size has a component per dimension (two for a cube) and one more for the layers
		unsigned sizeDims = cube ? 2 : dims;
		if (numSamples) {
			// a texture that is not multisampled has one sample
			resID = multisampled ? builder.encodeOp(Opcode::ImageQuerySamples, resultType, { image }) : builder.declareConstantScalar<int32_t>(1);
			return true;
		}
		if (numLevels) {
			// a storage image view covers one level
			resID = (storage || multisampled || texelBuffer) ? builder.declareConstantScalar<int32_t>(1) : builder.encodeOp(Opcode::ImageQueryLevels, resultType, { image });
			return true;
		}
		unsigned component = width ? 0 : (height ? 1 : (depthQuery ? 2 : sizeDims));
		if ((height && sizeDims < 2) || (depthQuery && imageType.imageDimensionality != Dim::e3D) || (arraySize && !arrayed) || (!arraySize && component >= sizeDims)) {
			throw ImpossibleResultID("size query of a dimension the texture does not have");
		}
		// a one-dimensional texture without layers has a scalar size
		auto scalarSize = sizeDims + (arrayed ? 1 : 0) == 1;
		auto sizeType = scalarSize ? int32Type : builder.declareType(Type(Type::VectorTag {}, sizeDims + (arrayed ? 1 : 0), int32Type));
		// a storage image, a multisample image or a texel buffer has no levels to choose from
		auto sizes = (storage || multisampled || texelBuffer)
			? builder.encodeOp(Opcode::ImageQuerySize, sizeType, { image })
			: builder.encodeOp(Opcode::ImageQuerySizeLod, sizeType, { image, arraySize ? builder.declareConstantScalar<int32_t>(0) : operand(1) });
		resID = scalarSize ? sizes : builder.encodeCompositeExtract(resultType, sizes, { component });
		return true;
	}

	// The texel comes back on its own or as the first member of a { texel, status } pair. A status of
	// zero means the access succeeded.
	auto resultTypeInst = builder.reverseLookupType(resultType);
	bool wrapped = resultTypeInst && resultTypeInst->backingType == Type::BackingType::Structure;
	auto valueTypeID = wrapped ? resultTypeInst->structureMembers.at(0).id : resultType;
	auto finish = [&](ResultID value) {
		resID = wrapped ? builder.encodeCompositeInsert(resultType, value, builder.declareNullValue(resultType), { 0 }) : value;
	};

	if (compare && arrayed) {
		throw ImpossibleResultID("depth comparison on an array texture is unsupported");
	}
	if (texelBuffer && (sample || gather)) {
		throw ImpossibleResultID("a texture buffer can only be read and written, not sampled or gathered");
	}
	bool cubeImage = imageType.imageDimensionality == Dim::eCube;
	if (gather && (dims == 1 || (dims == 3 && !cubeImage) || (cubeImage && arrayed))) {
		throw ImpossibleResultID("gather on a texture class that has no gather is unsupported");
	}

	// A read of a texture that is only read names a sampler (the result of air.get_read_sampler, which has no
	// value of its own) before the coordinates and, after them (and after the layer), an offset (not for one-dimensional textures). A texture that is also written
	// (and a write) has neither.
	bool readsWithSampler = read && argumentCount > 1 && isReadSampler(DynamicLLVM::LLVMGetOperand(inst, 1));
	// (a gather of a depth texture has a constant before the coordinates, as the comparing sample does)
	unsigned coordinatesIndex = (compare || gatherDepth || gatherCompare || readDepthMultisample) ? 3 : ((sample || gather || readsWithSampler) ? 2 : 1);

	// an array texture takes the layer as another coordinate (a float one when sampling)
	auto coordinates = operand(coordinatesIndex);
	// the coordinates without the layer, and the type of their components
	auto unlayeredCoordinates = coordinates;
	auto coordinatesLLVMType = DynamicLLVM::LLVMTypeOf(argument(coordinatesIndex));
	bool vectorCoordinates = DynamicLLVM::LLVMGetTypeKind(coordinatesLLVMType) == LLVMVectorTypeKind;
	auto coordinateComponentType = llvmTypeToSPIRVType(builder, vectorCoordinates ? DynamicLLVM::LLVMGetElementType(coordinatesLLVMType) : coordinatesLLVMType);
	if (arrayed) {
		auto coordinateType = *builder.reverseLookupType(builder.lookupResultType(coordinates));
		auto componentType = coordinateType.backingType == Type::BackingType::Vector ? coordinateType.targetType : builder.lookupResultType(coordinates);
		std::vector<uint32_t> components;
		for (unsigned i = 0; i < dims; ++i) {
			components.push_back(dims == 1 ? coordinates : builder.encodeCompositeExtract(componentType, coordinates, { i }));
		}
		auto layer = operand(coordinatesIndex + 1);
		if (sample || gather) {
			layer = builder.encodeOp(Opcode::ConvertSToF, componentType, { layer });
		}
		components.push_back(layer);
		auto compositeType = builder.declareType(Type(Type::VectorTag {}, dims + 1, componentType));
		coordinates = builder.encodeOp(Opcode::CompositeConstruct, compositeType, components);
	}
	unsigned layerOperands = arrayed ? 1 : 0;

	auto texelType = builder.declareType(Type(Type::VectorTag {}, 4, imageType.targetType));
	ResultID value;

	if (write) {
		// write(texture, coordinates, [layer,] color [, level]): only textures declared with write or
		// read_write access are storage images
		if (!storage) {
			throw ImpossibleResultID("write to a texture that is not declared writable");
		}
		// the level follows the color; a storage image view covers one level, so only level 0 is written
		// (a texture buffer has no levels: its last operand is a flag)
		auto level = argument(3 + layerOperands);
		if (!texelBuffer && (!DynamicLLVM::LLVMIsAConstantInt(level) || DynamicLLVM::LLVMConstIntGetZExtValue(level) != 0)) {
			throw ImpossibleResultID("texture write to a level other than 0 is unsupported");
		}
		auto color = operand(2 + layerOperands);
		builder.requireCapability(Capability::StorageImageWriteWithoutFormat);
		builder.encodeOpWithoutResult(Opcode::ImageWrite, { image, coordinates, convertTexel(builder, color, builder.lookupResultType(color), texelType) });
		return true;
	}

	if (gather) {
		// The operands after the coordinates (and the layer): gather() of a color texture has a flag, the offset
		// (2D textures only) and the component (0 to 3 for x to w); gather() of a depth texture the flag and the
		// offset, and gather_compare() the reference value in front of them. A trailing zero follows in each case.
		// The flag is true in every compiled kernel observed and does not change what the call does.
		unsigned next = coordinatesIndex + 1 + layerOperands;
		ResultID reference = 0;
		if (gatherCompare) {
			reference = operand(next++);
		}
		std::optional<unsigned> offsetIndex;
		if (!cubeImage) {
			offsetIndex = next + 1;
		}
		ResultID component = 0;
		if (gatherColor) {
			auto componentValue = argument(cubeImage ? next : next + 2);
			if (!DynamicLLVM::LLVMIsAConstantInt(componentValue) || DynamicLLVM::LLVMConstIntGetZExtValue(componentValue) > 3) {
				throw ImpossibleResultID("texture gather with a component that is not a constant between 0 and 3");
			}
			component = builder.declareConstantScalar<uint32_t>(static_cast<uint32_t>(DynamicLLVM::LLVMConstIntGetZExtValue(componentValue)));
		}
		auto sampler = loadSampler(argument(1));
		auto sampledType = builder.declareType(Type(Type::SampledImageTag {}, imageTypeID));
		auto sampled = builder.encodeSampledImage(sampledType, image, sampler);

		std::vector<uint32_t> arguments { sampled, coordinates };
		if (gatherCompare) {
			arguments.push_back(reference);
		} else if (gatherColor) {
			arguments.push_back(component);
		} else {
			// the components of a depth texture are all the depth value
			arguments.push_back(builder.declareConstantScalar<uint32_t>(0));
		}
		if (offsetIndex) {
			auto offset = argument(*offsetIndex);
			if (!DynamicLLVM::LLVMIsNull(offset)) {
				bool constantOffset;
				auto offsetID = airTexelOffset(builder, offset, constantOffset);
				if (constantOffset) {
					// image operands: ConstOffset = 0x8
					arguments.push_back(0x8);
					arguments.push_back(offsetID);
				} else {
					// a gather reads the base level
					arguments[1] = offsetSampleCoordinates(builder, image, dims, arrayed, coordinates, coordinateComponentType, offset, builder.declareConstantScalar<int32_t>(0));
				}
			}
		}

		if (gatherCompare) {
			finish(builder.encodeOp(Opcode::ImageDrefGather, valueTypeID, arguments));
			return true;
		}
		value = builder.encodeOp(Opcode::ImageGather, texelType, arguments);
	} else if (sample && isReadSampler(argument(1))) {
		} else if (sample) {
		auto sampler = loadSampler(argument(1));
		auto sampledType = builder.declareType(Type(Type::SampledImageTag {}, imageTypeID));
		auto sampled = builder.encodeSampledImage(sampledType, image, sampler);

		// The operands that follow the coordinates (and the layer, or the depth reference) are an offset
		// (not for cube textures), a flag that selects an explicit LOD, and that LOD.
		bool cube = imageType.imageDimensionality == Dim::eCube;
		unsigned lodFlagIndex = compare ? (cube ? 5 : 7) : (cube ? 3 : 5) + layerOperands;
		auto offset = cube ? nullptr : argument(lodFlagIndex - 1);
		auto lodFlag = argument(lodFlagIndex);
		if (!DynamicLLVM::LLVMIsAConstantInt(lodFlag)) {
			throw ImpossibleResultID("texture sample with a non-constant LOD mode");
		}
		// implicit LODs exist only in fragment functions
		bool givenLod = DynamicLLVM::LLVMConstIntGetZExtValue(lodFlag) != 0;
		bool explicitLod = givenLod || stage != Iridium::FunctionType::Fragment;
		bool hasOffset = offset && !DynamicLLVM::LLVMIsNull(offset);
		bool constantOffset = true;
		auto offsetID = hasOffset ? airTexelOffset(builder, offset, constantOffset) : ResultIDInvalid;
		auto floatType = builder.declareType(Type(Type::FloatTag {}, 32));
		auto lod = givenLod ? operand(lodFlagIndex + 1) : builder.declareConstantScalar<float>(0);

		// Vulkan has no sampling with an offset that is only known at run time, so such an offset moves the coordinates
		// instead (see offsetSampleCoordinates), by the size of the level the LOD selects.
		bool gradients = false;
		ResultID gradientX = ResultIDInvalid;
		ResultID gradientY = ResultIDInvalid;
		if (hasOffset && !constantOffset) {
			ResultID level;
			if (explicitLod) {
				level = givenLod ? offsetLevel(builder, image, lod, floatType) : builder.declareConstantScalar<int32_t>(0);
			} else {
				builder.requireCapability(Capability::ImageQuery);
				auto lodPair = builder.encodeOp(Opcode::ImageQueryLod, builder.declareType(Type(Type::VectorTag {}, 2, floatType)), { sampled, unlayeredCoordinates });
				level = offsetLevel(builder, image, builder.encodeCompositeExtract(floatType, lodPair, { 0 }), floatType);
				// The implicit LOD would follow the moved coordinates, which differ from the passed ones where the
				// offset differs between neighbouring fragments; take it from the passed coordinates instead.
				auto unlayeredType = llvmTypeToSPIRVType(builder, coordinatesLLVMType);
				gradientX = builder.encodeOp(Opcode::DPdx, unlayeredType, { unlayeredCoordinates });
				gradientY = builder.encodeOp(Opcode::DPdy, unlayeredType, { unlayeredCoordinates });
				gradients = true;
			}
			coordinates = offsetSampleCoordinates(builder, image, dims, arrayed, coordinates, coordinateComponentType, offset, level);
		}
		bool constOffsetOperand = hasOffset && constantOffset;

		std::vector<uint32_t> arguments { sampled, coordinates };
		if (compare) {
			arguments.push_back(operand(3 + layerOperands));
		}
		// image operands: Lod = 0x2, Grad = 0x4, ConstOffset = 0x8
		uint32_t imageOperands = (gradients ? 0x4 : (explicitLod ? 0x2 : 0)) | (constOffsetOperand ? 0x8 : 0);
		if (imageOperands) {
			arguments.push_back(imageOperands);
			if (gradients) {
				arguments.push_back(gradientX);
				arguments.push_back(gradientY);
			} else if (explicitLod) {
				arguments.push_back(lod);
			}
			if (constOffsetOperand) {
				arguments.push_back(offsetID);
			}
		}

		bool explicitSample = explicitLod || gradients;
		if (compare) {
			auto opcode = explicitSample ? Opcode::ImageSampleDrefExplicitLod : Opcode::ImageSampleDrefImplicitLod;
			finish(builder.encodeOp(opcode, valueTypeID, arguments));
			return true;
		}
		value = builder.encodeOp(explicitSample ? Opcode::ImageSampleExplicitLod : Opcode::ImageSampleImplicitLod, texelType, arguments);
	} else if (imageType.imageIsMultisampled) {
		// read(coordinates, [layer,] sample): a fetch of one sample, which has no level
		if (!read || storage) {
			throw ImpossibleResultID("multisample textures can only be read, with a sample index");
		}
		if (arrayed) {
			builder.requireCapability(Capability::ImageMSArray);
		}
		// image operands: Sample = 0x40
		value = builder.encodeOp(Opcode::ImageFetch, texelType, { image, coordinates, 0x40, operand(coordinatesIndex + 1 + layerOperands) });
	} else {
		std::vector<uint32_t> arguments { image, coordinates };
		if (!storage && !texelBuffer) {
			// a sampled image is read with OpImageFetch, which takes the level
			// (a one-dimensional texture has no offset)
			bool hasOffsetOperand = readsWithSampler && dims > 1;
			unsigned levelIndex = coordinatesIndex + layerOperands + (hasOffsetOperand ? 2 : 1);
			auto offset = hasOffsetOperand ? argument(levelIndex - 1) : nullptr;
			bool hasOffset = offset && !DynamicLLVM::LLVMIsNull(offset);
			bool constantOffset = true;
			auto offsetID = hasOffset ? airTexelOffset(builder, offset, constantOffset) : ResultIDInvalid;
			if (hasOffset && !constantOffset) {
				throw ImpossibleResultID("texture read with a non-constant offset");
			}
			bool constOffsetOperand = hasOffset && constantOffset;
			// image operands: Lod = 0x2, ConstOffset = 0x8
			arguments.push_back(0x2 | (constOffsetOperand ? 0x8 : 0));
			arguments.push_back(operand(levelIndex));
			if (constOffsetOperand) {
				arguments.push_back(offsetID);
			}
		}
		if (storage) {
			builder.requireCapability(Capability::StorageImageReadWithoutFormat);
		}
		value = builder.encodeOp(storage ? Opcode::ImageRead : Opcode::ImageFetch, texelType, arguments);
	}

	// a depth texture yields a single value, the first component of the fetched texel
	if (readDepthMultisample) {
		value = builder.encodeCompositeExtract(imageType.targetType, value, { 0 });
		texelType = imageType.targetType;
	}
	finish(convertTexel(builder, value, texelType, valueTypeID));
	return true;
}

// The direct members described by an air.struct_type_info node: each is an offset, a size, an array
// length, a type name and a name. Nested descriptions (a key string followed by its node) are skipped.
static std::vector<Iridium::StructMemberInfo> airStructMembers(LLVMValueRef node) {
	std::vector<LLVMValueRef> operands(DynamicLLVM::LLVMGetMDNodeNumOperands(node));
	DynamicLLVM::LLVMGetMDNodeOperands(node, operands.data());

	std::vector<Iridium::StructMemberInfo> members;
	for (size_t i = 0; i < operands.size();) {
		if (operands[i] && DynamicLLVM::LLVMIsAMDString(operands[i])) {
			i += 2;
			continue;
		}
		if (i + 4 >= operands.size() || !DynamicLLVM::LLVMIsAConstantInt(operands[i]) || !DynamicLLVM::LLVMIsAMDString(operands[i + 3]) || !DynamicLLVM::LLVMIsAMDString(operands[i + 4])) {
			// reflection only describes the shader, so stop at anything unrecognized rather than fail
			break;
		}
		members.push_back(Iridium::StructMemberInfo {
			static_cast<size_t>(DynamicLLVM::LLVMConstIntGetZExtValue(operands[i])),
			std::string(llvmMDStringToStringView(operands[i + 3])),
			std::string(llvmMDStringToStringView(operands[i + 4])),
		});
		i += 5;
	}
	return members;
}

// The packed state of a constant sampler, from the initializer of its global variable. The state is one 64-bit
// integer, or the first of the integers of an array (the compiler also emits a second word, which holds no
// sampler state this translation uses).
static uint64_t airSamplerStateWord(LLVMValueRef initializer) {
	if (initializer && DynamicLLVM::LLVMIsAConstantInt(initializer)) {
		return DynamicLLVM::LLVMConstIntGetZExtValue(initializer);
	}
	if (initializer && DynamicLLVM::LLVMGetValueKind(initializer) == LLVMConstantAggregateZeroValueKind) {
		return 0;
	}
	if (initializer && DynamicLLVM::LLVMGetValueKind(initializer) == LLVMConstantDataArrayValueKind) {
		auto word = DynamicLLVM::LLVMGetElementAsConstant(initializer, 0);
		if (word && DynamicLLVM::LLVMIsAConstantInt(word)) {
			return DynamicLLVM::LLVMConstIntGetZExtValue(word);
		}
	}
	throw ImpossibleResultID("constant sampler state is not an integer or an array of integers");
}

// The reflection information for a buffer, texture, sampler or vertex input parameter; other parameters
// (fragment stage inputs and built-ins) have none.
static std::optional<Iridium::ArgumentInfo> airArgumentInfo(LLVMValueRef parameterOperand) {
	auto info = airParameterOperands(parameterOperand);
	if (info.size() < 2 || !DynamicLLVM::LLVMIsAMDString(info[1])) {
		return std::nullopt;
	}

	Iridium::ArgumentInfo argument;
	auto kind = llvmMDStringToStringView(info[1]);
	if (kind == "air.buffer") {
		argument.type = Iridium::BindingType::Buffer;
	} else if (kind == "air.texture") {
		argument.type = Iridium::BindingType::Texture;
	} else if (kind == "air.sampler") {
		argument.type = Iridium::BindingType::Sampler;
	} else if (kind == "air.vertex_input") {
		argument.type = Iridium::BindingType::VertexInput;
	} else {
		return std::nullopt;
	}

	auto integer = [](LLVMValueRef value) -> size_t {
		return DynamicLLVM::LLVMIsAConstantInt(value) ? DynamicLLVM::LLVMConstIntGetZExtValue(value) : 0;
	};
	auto string = [](LLVMValueRef value) {
		return DynamicLLVM::LLVMIsAMDString(value) ? std::string(llvmMDStringToStringView(value)) : std::string();
	};

	bool hasLocationIndex = false;
	for (size_t i = 2; i + 1 < info.size(); ++i) {
		if (!info[i] || !DynamicLLVM::LLVMIsAMDString(info[i])) {
			continue;
		}
		auto key = llvmMDStringToStringView(info[i]);
		auto value = info[i + 1];
		if (key == "air.location_index") {
			argument.index = integer(value);
			hasLocationIndex = true;
		} else if (key == "air.arg_name") {
			argument.name = string(value);
		} else if (key == "air.arg_type_name") {
			argument.typeName = string(value);
		} else if (key == "air.arg_type_size") {
			argument.size = integer(value);
		} else if (key == "air.arg_type_align_size") {
			argument.alignment = integer(value);
		} else if (key == "air.struct_type_info" && value && !DynamicLLVM::LLVMIsAMDString(value)) {
			argument.members = airStructMembers(value);
		}
	}

	// A vertex attribute is identified by its location; there is no other index to fall back on.
	if (argument.type == Iridium::BindingType::VertexInput && !hasLocationIndex) {
		return std::nullopt;
	}

	return argument;
}

// Specialization IDs 0 to 2 are the threadgroup size of a kernel. The constant with Metal index N has SpecId
// firstFunctionConstantSpecializationID + N, and its "defined" flag the SpecId one range further.
static constexpr uint32_t firstFunctionConstantSpecializationID = 3;
static constexpr size_t maxFunctionConstantIndex = 65535;

void Iridium::AIR::Function::analyze(SPIRV::Builder& builder, OutputInfo& outputInfo) {
	//auto tmp = DynamicLLVM::LLVMPrintModuleToString(_module.get());
	//std::cout << "    " << tmp << std::endl;
	//DynamicLLVM::LLVMDisposeMessage(tmp);

	auto& funcInfo = outputInfo.functionInfos[_name];

	auto namedMD = DynamicLLVM::LLVMGetFirstNamedMetadata(_module.get());

	auto voidType = builder.declareType(SPIRV::Type(SPIRV::Type::VoidTag {}));
	auto funcType = builder.declareType(SPIRV::Type(SPIRV::Type::FunctionTag {}, voidType, {}, 8));
	auto funcID = builder.declareFunction(funcType);
	auto intType = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 32, true));
	auto ptrIntType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, intType, 8));
	auto floatTypeInst = SPIRV::Type(SPIRV::Type::FloatTag {}, 32);
	auto floatType = builder.declareType(floatTypeInst);
	auto vec4Type = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 4, floatType));
	auto u32Type = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 32, false));
	auto uvec3Type = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 3, u32Type));

	SPIRV::ResultID perVertexVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID vertexIndexVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID fragCoordVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID uboPointersVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID computeLocalSizeX = SPIRV::ResultIDInvalid;
	SPIRV::ResultID computeLocalSizeY = SPIRV::ResultIDInvalid;
	SPIRV::ResultID computeLocalSizeZ = SPIRV::ResultIDInvalid;
	SPIRV::ResultID globalInvocationIdVar = SPIRV::ResultIDInvalid;
	std::unordered_map<uint32_t, SPIRV::ResultID> computeInputVars;

	// The Input variable of a compute built-in, declared on first use.
	auto computeInput = [&](SPIRV::BuiltinID builtin, SPIRV::ResultID valueType) {
		auto [it, inserted] = computeInputVars.try_emplace(static_cast<uint32_t>(builtin), SPIRV::ResultIDInvalid);
		if (inserted) {
			auto pointerType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, valueType, 8));
			it->second = builder.addGlobalVariable(pointerType, SPIRV::StorageClass::Input);
			builder.addDecoration(it->second, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(builtin) } });
			builder.referenceGlobalVariable(it->second);
		}
		return builder.encodeLoad(valueType, it->second);
	};

	auto firstLabelID = builder.beginFunction(funcID.id);

	// within this node, operand 0 refers to the vertex function,
	// operand 1 contains information about the function's return value,
	// and operand 2 contains information about the function's parameters.
	std::vector<LLVMValueRef> rootInfoOperands;
	const char* stages[] = { "air.vertex", "air.fragment", "air.kernel" };
	size_t selectedStage = SIZE_MAX;
	for (size_t stage = 0; stage < 3; ++stage) {
		auto count = DynamicLLVM::LLVMGetNamedMetadataNumOperands(_module.get(), stages[stage]);
		std::vector<LLVMValueRef> entries(count);
		DynamicLLVM::LLVMGetNamedMetadataOperands(_module.get(), stages[stage], entries.data());
		for (auto entry : entries) {
			auto fields = DynamicLLVM::LLVMGetMDNodeNumOperands(entry);
			if (!fields) continue;
			std::vector<LLVMValueRef> info(fields);
			DynamicLLVM::LLVMGetMDNodeOperands(entry, info.data());
			if (info[0] != _function) continue;
			if (fields < 3 || selectedStage != SIZE_MAX || !info[1] || !info[2]) {
				throw ImpossibleResultID("entry function has malformed or duplicate stage metadata");
			}
			selectedStage = stage;
			rootInfoOperands = std::move(info);
		}
	}
	if (selectedStage == SIZE_MAX) {
		throw ImpossibleResultID("entry function has no matching stage metadata");
	}

	if (selectedStage == 0) {
		// this is a vertex shader

		funcInfo.type = FunctionType::Vertex;

		builder.addEntryPoint(SPIRV::EntryPoint {
			SPIRV::ExecutionModel::Vertex,
			funcID.id,
			_name,
			{},
		});

		// declare the GLSL PerVertex structure. An output interface block carries no explicit layout.
		SPIRV::Type perVertexStructInst(SPIRV::Type::StructureTag {}, {
			SPIRV::Type::Member {
				vec4Type,
				0,
				{
					SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::Position) } },
				}
			},
			// TODO: declare the rest of the structure
		}, floatTypeInst.size * 4, floatTypeInst.alignment);
		perVertexStructInst.layoutFree = true;
		auto perVertexStructType = builder.declareType(perVertexStructInst);
		auto perVertexStructPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, perVertexStructType, 8));
		perVertexVar = builder.addGlobalVariable(perVertexStructPtrType, SPIRV::StorageClass::Output);

		builder.addDecoration(perVertexStructType, SPIRV::Decoration { SPIRV::DecorationType::Block });

		builder.referenceGlobalVariable(perVertexVar);

		// Indium can draw points with any vertex function, and Vulkan then requires the point size to be
		// written. Metal's default point size is 1.
		auto pointSizePtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, floatType, 8));
		auto pointSizeVar = builder.addGlobalVariable(pointSizePtrType, SPIRV::StorageClass::Output);
		builder.addDecoration(pointSizeVar, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::PointSize) } });
		builder.referenceGlobalVariable(pointSizeVar);
		builder.encodeStore(pointSizeVar, builder.declareConstantScalar<float>(1.0f));

		// declare the GLSL VertexIndex input variable
		vertexIndexVar = builder.addGlobalVariable(ptrIntType, SPIRV::StorageClass::Input);

		builder.addDecoration(vertexIndexVar, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::VertexIndex) } });

		builder.referenceGlobalVariable(vertexIndexVar);

	} else if (selectedStage == 1) {
		// this is a fragment shader

		funcInfo.type = FunctionType::Fragment;

		builder.addEntryPoint(SPIRV::EntryPoint {
			SPIRV::ExecutionModel::Fragment,
			funcID.id,
			_name,
			{},
		});

		// declare the fragment coordinate input variable
		auto ptrVec4Type = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, vec4Type, 8));
		fragCoordVar = builder.addGlobalVariable(ptrVec4Type, SPIRV::StorageClass::Input);

		builder.addDecoration(fragCoordVar, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::FragCoord) } });

		builder.referenceGlobalVariable(fragCoordVar);

	} else if (selectedStage == 2) {
		// this is a "kernel" (compute shader)

		funcInfo.type = FunctionType::Kernel;

		// declare the local size specialized constants
		computeLocalSizeX = builder.declareConstantScalar<uint32_t>(0, 0);
		computeLocalSizeY = builder.declareConstantScalar<uint32_t>(0, 1);
		computeLocalSizeZ = builder.declareConstantScalar<uint32_t>(0, 2);

		// declare the entry point
		builder.addEntryPoint(SPIRV::EntryPoint {
			SPIRV::ExecutionModel::GLCompute,
			funcID.id,
			_name,
			{},
			{
				{ SPIRV::ExecutionMode::LocalSizeId, true, {
					computeLocalSizeX,
					computeLocalSizeY,
					computeLocalSizeZ,
				} },
			},
		});

		// declare the various global/local/subgroup ids and indices
		auto ptrUVec3Type = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, uvec3Type, 8));
		globalInvocationIdVar = builder.addGlobalVariable(ptrUVec3Type, SPIRV::StorageClass::Input);

		builder.addDecoration(globalInvocationIdVar, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::GlobalInvocationId) } });

		builder.referenceGlobalVariable(globalInvocationIdVar);

		// the operands after the parameters describe the kernel as a whole, each a node of a key and its value.
		// air.max_work_group_size is the limit MSL's [[max_total_threads_per_threadgroup(N)]] sets.
		for (size_t i = 3; i < rootInfoOperands.size(); ++i) {
			auto attribute = rootInfoOperands[i];
			if (!attribute || DynamicLLVM::LLVMGetValueKind(attribute) != LLVMMetadataAsValueValueKind || DynamicLLVM::LLVMIsAMDString(attribute)) {
				continue;
			}
			std::vector<LLVMValueRef> fields(DynamicLLVM::LLVMGetMDNodeNumOperands(attribute));
			DynamicLLVM::LLVMGetMDNodeOperands(attribute, fields.data());
			if (fields.empty() || !fields[0] || !DynamicLLVM::LLVMIsAMDString(fields[0]) || llvmMDStringToStringView(fields[0]) != "air.max_work_group_size") {
				continue;
			}
			if (fields.size() != 2 || !fields[1] || !DynamicLLVM::LLVMIsAConstantInt(fields[1]) ||
				DynamicLLVM::LLVMGetIntTypeWidth(DynamicLLVM::LLVMTypeOf(fields[1])) != 32) {
				throw ImpossibleResultID("air.max_work_group_size is not a 32-bit integer");
			}
			funcInfo.maxTotalThreadsPerThreadgroup = DynamicLLVM::LLVMConstIntGetZExtValue(fields[1]);
			if (funcInfo.maxTotalThreadsPerThreadgroup == 0) {
				throw ImpossibleResultID("air.max_work_group_size is 0");
			}
		}
	}

	// TODO: inspect the output of some more compiled shaders; the current code is only valid for
	//       shaders that return simple structures, vectors, or scalars as outputs.

	// SPIR-V doesn't allow structures to have a storage class of "output", so
	// if the function returns a structure, we need to separate the components
	// into separate output variables.

	// With opaque pointers LLVMTypeOf(_function) is just "ptr", and
	// LLVMGetElementType on that returns null, which used to segfault in
	// LLVMGetReturnType below. LLVMGlobalGetValueType is the correct way to
	// get a global's or function's own type.
	auto llfuncType = DynamicLLVM::LLVMGlobalGetValueType(_function);
	auto funcRetType = DynamicLLVM::LLVMGetReturnType(llfuncType);
	std::vector<LLVMTypeRef> funcParamTypes(DynamicLLVM::LLVMCountParamTypes(llfuncType));
	DynamicLLVM::LLVMGetParamTypes(llfuncType, funcParamTypes.data());

	// With opaque pointers an argument's LLVM type is just "ptr addrspace(n)",
	// so the real type of every non-buffer argument has to come from its AIR
	// descriptor rather than from funcParamTypes.
	auto airArgumentType = [&](LLVMValueRef parameterOperand) {
		auto typeNameNode = findAIMDValue(parameterOperand, "air.arg_type_name");
		if (!typeNameNode) {
			throw ImpossibleResultID("argument descriptor has no air.arg_type_name");
		}
		return spirvTypeForAIRTypeName(builder, _module.get(), llvmMDStringToStringView(typeNameNode));
	};

	std::vector<LLVMValueRef> returnValueOperands(DynamicLLVM::LLVMGetMDNodeNumOperands(rootInfoOperands[1]));
	DynamicLLVM::LLVMGetMDNodeOperands(rootInfoOperands[1], returnValueOperands.data());

	auto isPositionOutput = [](LLVMValueRef descriptor) {
		std::vector<LLVMValueRef> info(DynamicLLVM::LLVMGetMDNodeNumOperands(descriptor));
		DynamicLLVM::LLVMGetMDNodeOperands(descriptor, info.data());
		return std::any_of(info.begin(), info.end(), [](LLVMValueRef value) {
			return DynamicLLVM::LLVMIsAMDString(value) && llvmMDStringToStringView(value) == "air.position";
		});
	};

	if (DynamicLLVM::LLVMGetTypeKind(funcRetType) == LLVMStructTypeKind) {
		// analyze return value and mark special values (like the position)
		uint32_t location = 0;
		for (size_t i = 0; i < returnValueOperands.size(); ++i) {
			auto& returnValueOperand = returnValueOperands[i];

			bool isSpecial = isPositionOutput(returnValueOperand);
			if (isSpecial) {
				_positionOutputIndex = i;
				_outputValueIDs.push_back(SPIRV::ResultIDInvalid);
			}

			if (!isSpecial) {
				auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMStructGetTypeAtIndex(funcRetType, i));
				auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type, 8));
				auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Output);
				builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { airInterfaceLocation(returnValueOperand, location) } });
				++location;
				_outputValueIDs.push_back(var);
				builder.referenceGlobalVariable(var);
			}
		}
	} else if (DynamicLLVM::LLVMGetTypeKind(funcRetType) != LLVMVoidTypeKind) {
		auto type = llvmTypeToSPIRVType(builder, funcRetType);
		if (!returnValueOperands.empty() && isPositionOutput(returnValueOperands[0])) {
			if (funcInfo.type != FunctionType::Vertex || type != vec4Type) {
				throw ImpossibleResultID("unsupported single position output type");
			}
			_positionOutputIndex = 0;
			_outputValueIDs.push_back(SPIRV::ResultIDInvalid);
		} else {
			auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type, 8));
			auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Output);
			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { returnValueOperands.empty() ? 0 : airInterfaceLocation(returnValueOperands[0], 0) } });
			_outputValueIDs.push_back(var);
			builder.referenceGlobalVariable(var);
		}
	}

	std::vector<LLVMValueRef> parameterOperands(DynamicLLVM::LLVMGetMDNodeNumOperands(rootInfoOperands[2]));
	DynamicLLVM::LLVMGetMDNodeOperands(rootInfoOperands[2], parameterOperands.data());

	for (auto parameterOperand: parameterOperands) {
		if (auto argument = airArgumentInfo(parameterOperand)) {
			funcInfo.arguments.push_back(std::move(*argument));
		}
	}

	//
	// function constants
	//
	// Each constant becomes a specialization constant, with a second boolean one telling whether the
	// application gave it a value. The module's static initializer, which copies the constants into the
	// globals the function reads and derives the constants built from them, is translated as a prefix of
	// the function (see `initializerInstructions`), so reads of those globals see ordinary values.
	std::unordered_map<LLVMValueRef, SPIRV::ResultID> functionConstantDefined;
	// the instructions of the module's static initializer (everything before its return), which only
	// exists when the module has function constants
	std::vector<LLVMValueRef> initializerInstructions;
	for (auto function = DynamicLLVM::LLVMGetFirstFunction(_module.get()); function; function = DynamicLLVM::LLVMGetNextFunction(function)) {
		auto section = DynamicLLVM::LLVMGetSection(function);
		if (!section || std::string_view(section) != "air.static_init") {
			continue;
		}
		auto block = DynamicLLVM::LLVMGetFirstBasicBlock(function);
		if (!block || DynamicLLVM::LLVMGetNextBasicBlock(block)) {
			throw ImpossibleResultID("the static initializer does not consist of one basic block");
		}
		for (auto inst = DynamicLLVM::LLVMGetFirstInstruction(block); inst; inst = DynamicLLVM::LLVMGetNextInstruction(inst)) {
			if (DynamicLLVM::LLVMGetInstructionOpcode(inst) == LLVMRet) {
				break;
			}
			initializerInstructions.push_back(inst);
		}
	}
	{
		auto constantCount = DynamicLLVM::LLVMGetNamedMetadataNumOperands(_module.get(), "air.function_constants");
		std::vector<LLVMValueRef> constantEntries(constantCount);
		DynamicLLVM::LLVMGetNamedMetadataOperands(_module.get(), "air.function_constants", constantEntries.data());
		for (auto entry: constantEntries) {
			std::vector<LLVMValueRef> fields(DynamicLLVM::LLVMGetMDNodeNumOperands(entry));
			DynamicLLVM::LLVMGetMDNodeOperands(entry, fields.data());
			if (fields.size() < 4 || !DynamicLLVM::LLVMIsAMDString(fields[1]) || !DynamicLLVM::LLVMIsAMDString(fields[2]) || !DynamicLLVM::LLVMIsAConstantInt(fields[3])) {
				throw ImpossibleResultID("malformed air.function_constants entry");
			}

			FunctionConstantInfo info;
			auto typeName = llvmMDStringToStringView(fields[1]);
			info.name = std::string(llvmMDStringToStringView(fields[2]));
			info.index = DynamicLLVM::LLVMConstIntGetZExtValue(fields[3]);
			if (info.index > maxFunctionConstantIndex) {
				throw ImpossibleResultID("function constant index out of range");
			}
			info.specializationID = firstFunctionConstantSpecializationID + info.index;
			info.definedSpecializationID = firstFunctionConstantSpecializationID + maxFunctionConstantIndex + 1 + info.index;

			auto global = fields[0];
			auto storageType = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGlobalGetValueType(global));
			SPIRV::ResultID value = SPIRV::ResultIDInvalid;
			if (typeName == "bool") {
				info.type = FunctionConstantInfo::Type::Bool;
				value = builder.declareConstantScalar<bool>(false, info.specializationID);
			} else if (typeName == "int" || typeName == "uint") {
				// the 32 bits are the same whichever way they are read, and LLVM has no unsigned integers
				info.type = typeName == "int" ? FunctionConstantInfo::Type::Int : FunctionConstantInfo::Type::UInt;
				value = builder.declareConstantScalar<int32_t>(0, info.specializationID);
			} else if (typeName == "float") {
				info.type = FunctionConstantInfo::Type::Float;
				value = builder.declareConstantScalar<float>(0, info.specializationID);
			} else {
				throw ImpossibleResultID("unsupported function constant type " + std::string(typeName));
			}

			auto pointerType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Private, storageType, 8));
			auto variable = builder.addGlobalVariable(pointerType, SPIRV::StorageClass::Private);
			builder.setResultType(variable, pointerType);
			builder.associateExistingResultID(variable, reinterpret_cast<uintptr_t>(global));
			functionConstantDefined[global] = builder.declareConstantScalar<bool>(false, info.definedSpecializationID);

			if (info.type == FunctionConstantInfo::Type::Bool) {
				// LLVM keeps a bool in a byte
				value = builder.encodeOp(SPIRV::Opcode::Select, storageType, { value, builder.declareConstantScalar<int8_t>(1), builder.declareConstantScalar<int8_t>(0) });
			}
			builder.encodeStore(variable, value);

			funcInfo.functionConstants.push_back(std::move(info));
		}
	}

	//
	// analyze parameters and mark special values (like the vertex ID)
	//

	// first, count how many buffers we have (we'll need this later)
	uint32_t bufferCount = 0;
	for (size_t i = 0; i < parameterOperands.size(); ++i) {
		auto& parameterOperand = parameterOperands[i];

		// info[0] is the parameter index
		// info[1] is the kind
		// everything after that depends on the kind
		auto parameterInfo = airParameterOperands(parameterOperand);

		auto kind = airParameterKind(parameterInfo);

		if (kind == "air.buffer") {
			++bufferCount;
		}
	}

	std::vector<SPIRV::Type::Member> bufferMembers;
	if (bufferCount > 0) {
		// if we have buffers, add a global variable for the UBO that contains their physical addresses

		uint32_t bufferIndex = 0;
		for (size_t i = 0; i < parameterOperands.size(); ++i) {
			auto& parameterOperand = parameterOperands[i];

			// info[0] is the parameter index
			// info[1] is the kind
			// everything after that depends on the kind
			auto parameterInfo = airParameterOperands(parameterOperand);

			auto kind = airParameterKind(parameterInfo);

			if (kind == "air.buffer") {
				auto typeNameNode = findAIMDValue(parameterOperand, "air.arg_type_name");
				if (!typeNameNode) {
					throw ImpossibleResultID("air.buffer descriptor has no air.arg_type_name");
				}
				auto typeName = llvmMDStringToStringView(typeNameNode);
				std::string_view base;
				size_t count = 0;
				if (typeName.substr(0, 7) == "packed_" && splitMetalTypeName(typeName, base, count) && count == 3) {
					if (auto scalar = spirvScalarTypeForMetalBase(base)) {
						_packed3Buffers.emplace(DynamicLLVM::LLVMGetParam(_function, i), "argument " + std::to_string(i) + " declared " +
							std::string(typeName) + " (" + std::to_string(3 * scalar->size) + "-byte elements)");
					}
				}
				bufferMembers.push_back(SPIRV::Type::Member { physicalPointerType(builder), 8 * bufferIndex, {} });
				++bufferIndex;
			}
		}

		auto structType = builder.declareType(SPIRV::Type(SPIRV::Type::StructureTag {}, bufferMembers, bufferIndex * 8, 8));
		auto structPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Uniform, structType, 8));

		builder.addDecoration(structType, SPIRV::Decoration { SPIRV::DecorationType::Block, {} });

		uboPointersVar = builder.addGlobalVariable(structPtrType, SPIRV::StorageClass::Uniform);
		builder.referenceGlobalVariable(uboPointersVar);

		builder.addDecoration(uboPointersVar, SPIRV::Decoration { SPIRV::DecorationType::DescriptorSet, { funcInfo.type == FunctionType::Fragment ? 1u : 0u } });
		builder.addDecoration(uboPointersVar, SPIRV::Decoration { SPIRV::DecorationType::Binding, { 0 } });
	}

	uint32_t paramLocation = 0;
	uint32_t bufferIndex = 0;

	// internal binding indices are the actual bindings we expect Indium to bind resources with.
	//
	// as required by Indium, we must assign indices in the following order:
	//   1. buffers (really only 1 index for all buffers)
	//   2. stage-ins
	//   3. textures
	//   4. samplers
	size_t internalBindingIndex = 0;

	if (bufferCount > 0) {
		// we put all our buffer addresses into a single UBO which occupies a single internal binding
		++internalBindingIndex;
	}

	for (size_t i = 0; i < parameterOperands.size(); ++i) {
		auto& parameterOperand = parameterOperands[i];

		// info[0] is the parameter index
		// info[1] is the kind
		// everything after that depends on the kind
		auto parameterInfo = airParameterOperands(parameterOperand);

		auto kind = airParameterKind(parameterInfo);
		auto llparam = DynamicLLVM::LLVMGetParam(_function, i);
		auto llparamVal = reinterpret_cast<uintptr_t>(llparam);

		if (kind == "air.vertex_id") {
			_vertexIDInputIndex = i;
			auto parameterType = DynamicLLVM::LLVMTypeOf(llparam);
			if (DynamicLLVM::LLVMGetTypeKind(parameterType) != LLVMIntegerTypeKind ||
				(DynamicLLVM::LLVMGetIntTypeWidth(parameterType) != 16 && DynamicLLVM::LLVMGetIntTypeWidth(parameterType) != 32)) {
				throw ImpossibleResultID("vertex_id must be a 16-bit or 32-bit integer");
			}
			auto type = llvmTypeToSPIRVType(builder, parameterType);
			auto load = builder.encodeLoad(intType, vertexIndexVar);
			auto value = DynamicLLVM::LLVMGetIntTypeWidth(parameterType) == 16
				? builder.encodeUConvert(type, load) : builder.encodeBitcast(type, load);
			_parameterIDs.push_back(value);
			builder.associateExistingResultID(value, llparamVal);
			builder.setResultType(value, type);
		} else if (kind == "air.buffer") {
			// find the location index info
			size_t infoIdx = 0;
			for (; infoIdx < parameterInfo.size(); ++infoIdx) {
				if (parameterInfo[infoIdx] && DynamicLLVM::LLVMIsAMDString(parameterInfo[infoIdx])) {
					auto str = llvmMDStringToStringView(parameterInfo[infoIdx]);

					if (str == "air.location_index") {
						break;
					}
				}
			}

			if (infoIdx >= parameterInfo.size()) {
				// a vertex attribute is identified by its location, there is nothing to fall back on
				throw ImpossibleResultID("air.vertex_input descriptor has no air.location_index");
			}

			uint32_t bindingIndex = airParameterInteger(parameterInfo, infoIdx + 1);

			funcInfo.bindings.push_back(BindingInfo { BindingType::Buffer, bindingIndex, 0 });

			auto ptrType = bufferMembers[bufferIndex].id;
			auto ptrPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Uniform, ptrType, 8));
			auto access = builder.encodeAccessChain(ptrPtrType, uboPointersVar, { builder.declareConstantScalar<int32_t>(bufferIndex) });
			auto load = builder.encodeLoad(ptrType, access);

			_parameterIDs.push_back(load);

			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, ptrType);

			++bufferIndex;
		} else if (kind == "air.instance_id") {
			auto var = builder.addGlobalVariable(ptrIntType, SPIRV::StorageClass::Input);
			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::InstanceIndex) } });
			builder.referenceGlobalVariable(var);
			auto load = builder.encodeLoad(intType, var);
			_parameterIDs.push_back(load);
			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, intType);
		} else if (kind == "air.front_facing") {
			auto boolType = builder.declareType(SPIRV::Type(SPIRV::Type::BooleanTag {}));
			auto boolPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, boolType, 8));
			auto var = builder.addGlobalVariable(boolPtrType, SPIRV::StorageClass::Input);
			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::FrontFacing) } });
			builder.referenceGlobalVariable(var);
			auto load = builder.encodeLoad(boolType, var);
			_parameterIDs.push_back(load);
			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, boolType);
		} else if (kind == "air.position") {
			auto load = builder.encodeLoad(vec4Type, fragCoordVar);
			_parameterIDs.push_back(load);
			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, vec4Type);
		} else if (kind == "air.fragment_input") {
			auto type = airArgumentType(parameterOperand);
			auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, type, 8));
			auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Input);
			auto load = builder.encodeLoad(type, var);

			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { airInterfaceLocation(parameterOperand, paramLocation) } });
			++paramLocation;

			builder.referenceGlobalVariable(var);

			_parameterIDs.push_back(load);

			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, type);
		} else if (kind == "air.vertex_input") {
			auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
			// Vulkan matches an input's numeric type against the vertex attribute format, so an unsigned
			// attribute needs an unsigned input. LLVM integers have no sign; the AIR type name keeps it.
			auto inputType = type;
			if (auto typeNameNode = findAIMDValue(parameterOperand, "air.arg_type_name")) {
				auto typeName = llvmMDStringToStringView(typeNameNode);
				if (typeName.substr(0, 4) == "uint" || typeName.substr(0, 6) == "ushort" || typeName.substr(0, 5) == "uchar" || typeName.substr(0, 5) == "ulong") {
					inputType = integerTypeWithSign(builder, type, false);
				}
			}
			auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, inputType, 8));
			auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Input);
			auto load = builder.encodeLoad(inputType, var);
			if (inputType != type) {
				load = builder.encodeBitcast(type, load);
			}

			// find the location index info
			size_t infoIdx = 0;
			for (; infoIdx < parameterInfo.size(); ++infoIdx) {
				if (parameterInfo[infoIdx] && DynamicLLVM::LLVMIsAMDString(parameterInfo[infoIdx])) {
					auto str = llvmMDStringToStringView(parameterInfo[infoIdx]);

					if (str == "air.location_index") {
						break;
					}
				}
			}

			if (infoIdx >= parameterInfo.size()) {
				// a vertex attribute is identified by its location, there is nothing to fall back on
				throw ImpossibleResultID("air.vertex_input descriptor has no air.location_index");
			}

			uint32_t locationIndex = airParameterInteger(parameterInfo, infoIdx + 1);

			funcInfo.bindings.push_back(BindingInfo { BindingType::VertexInput, locationIndex, /* ignored: */ 0, /* ignored: */ TextureAccessType::Read, /* ignored: */ 0 });

			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { locationIndex } });

			builder.referenceGlobalVariable(var);

			_parameterIDs.push_back(load);

			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, type);
		} else if (kind == "air.thread_index_in_simdgroup" || kind == "air.threads_per_simdgroup" || kind == "air.simdgroup_index_in_threadgroup"
				|| kind == "air.simdgroups_per_threadgroup") {
			// the subgroup of the invocation, as the number the parameter's integer type holds
			builder.requireCapability(SPIRV::Capability::GroupNonUniform);
			auto builtin = kind == "air.thread_index_in_simdgroup" ? SPIRV::BuiltinID::SubgroupLocalInvocationId
				: (kind == "air.threads_per_simdgroup" ? SPIRV::BuiltinID::SubgroupSize
				: (kind == "air.simdgroup_index_in_threadgroup" ? SPIRV::BuiltinID::SubgroupId : SPIRV::BuiltinID::NumSubgroups));
			auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
			auto load = computeInput(builtin, u32Type);
			if (type != u32Type) {
				// a 32-bit integer of the other signedness, or a narrower or wider one
				auto parameterType = *builder.reverseLookupType(type);
				load = parameterType.backingType == SPIRV::Type::BackingType::Integer && parameterType.scalarWidth == 32 ? builder.encodeBitcast(type, load) : builder.encodeUConvert(type, load);
			}
			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, type);
		} else if (kind == "air.thread_index_in_threadgroup") {
			auto load = computeInput(SPIRV::BuiltinID::LocalInvocationIndex, u32Type);
			auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
			if (type != u32Type) {
				auto parameterType = *builder.reverseLookupType(type);
				load = parameterType.backingType == SPIRV::Type::BackingType::Integer && parameterType.scalarWidth == 32 ? builder.encodeBitcast(type, load) : builder.encodeUConvert(type, load);
			}
			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, type);
		} else if (kind == "air.thread_position_in_grid" || kind == "air.thread_position_in_threadgroup" || kind == "air.threadgroup_position_in_grid"
				|| kind == "air.threadgroups_per_grid" || kind == "air.threads_per_threadgroup" || kind == "air.threads_per_grid") {
			auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
			SPIRV::ResultID load;
			if (kind == "air.thread_position_in_grid") {
				load = builder.encodeLoad(uvec3Type, globalInvocationIdVar);
			} else if (kind == "air.thread_position_in_threadgroup") {
				load = computeInput(SPIRV::BuiltinID::LocalInvocationId, uvec3Type);
			} else if (kind == "air.threadgroup_position_in_grid") {
				load = computeInput(SPIRV::BuiltinID::WorkgroupId, uvec3Type);
			} else if (kind == "air.threadgroups_per_grid") {
				load = computeInput(SPIRV::BuiltinID::NumWorkgroups, uvec3Type);
			} else {
				// the threadgroup size is the specialization constants the dispatch sets
				load = builder.encodeOp(SPIRV::Opcode::CompositeConstruct, uvec3Type, { computeLocalSizeX, computeLocalSizeY, computeLocalSizeZ });
				if (kind == "air.threads_per_grid") {
					load = builder.encodeArithBinop(SPIRV::Opcode::IMul, uvec3Type, load, computeInput(SPIRV::BuiltinID::NumWorkgroups, uvec3Type));
				}
			}
			auto typeInst = *builder.reverseLookupType(type);
			auto target = load;
			auto resultType = uvec3Type;

			// it's not clear what should happen with less than 3-component vectors (at least i can't find any documentation on it);
			// they could either be restricted to fewer dimensions (i.e. only X or only X and Y)
			// or have omitted dimensions multiplied and added in to the available dimensions (e.g. Y = Y + (max Y * Z)),
			// but the first approach is more likely so that's what we assume
			if (typeInst.backingType == SPIRV::Type::BackingType::Integer) {
				// extract just the X component
				target = builder.encodeCompositeExtract(u32Type, load, { 0 });
				resultType = u32Type;
			} else if (typeInst.entryCount == 2) {
				// extract the X and Y components
				auto uvec2Type = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 2, u32Type));
				target = builder.encodeVectorShuffle(uvec2Type, load, load, { 0, 1 });
				resultType = uvec2Type;
			}

			// the parameter's LLVM type has no signedness and maps to a signed integer, while the builtins are unsigned
			if (resultType != type) {
				auto elementOf = [&](const SPIRV::Type& inst) { return inst.backingType == SPIRV::Type::BackingType::Vector ? *builder.reverseLookupType(inst.targetType) : inst; };
				bool sameWidth = elementOf(*builder.reverseLookupType(resultType)).scalarWidth == elementOf(typeInst).scalarWidth;
				target = sameWidth ? builder.encodeBitcast(type, target) : builder.encodeUConvert(type, target);
				resultType = type;
			}

			builder.associateExistingResultID(target, llparamVal);
			builder.setResultType(target, resultType);
		}
	}

	// now look for textures and assign bindings for them
	for (size_t i = 0; i < parameterOperands.size(); ++i) {
		auto& parameterOperand = parameterOperands[i];

		// info[0] is the parameter index
		// info[1] is the kind
		// everything after that depends on the kind
		auto parameterInfo = airParameterOperands(parameterOperand);

		auto kind = airParameterKind(parameterInfo);

		if (kind == "air.texture") {
			// find the location index info, determine the access type, and find the arg type info
			size_t infoIdx = SIZE_MAX;
			TextureAccessType accessType = TextureAccessType::Sample;
			size_t argTypeIdx = SIZE_MAX;
			for (size_t idx = 0; idx < parameterInfo.size(); ++idx) {
				if (parameterInfo[idx] && DynamicLLVM::LLVMIsAMDString(parameterInfo[idx])) {
					auto str = llvmMDStringToStringView(parameterInfo[idx]);

					if (str == "air.location_index") {
						infoIdx = idx;
					} else if (str == "air.sample") {
						accessType = TextureAccessType::Sample;
					} else if (str == "air.arg_type_name") {
						argTypeIdx = idx;
					}
				}
			}

			if (infoIdx >= parameterInfo.size()) {
				// weird, location index info not found
				throw ImpossibleResultID("a texture or sampler description has no air.location_index");
			}

			if (argTypeIdx >= parameterInfo.size()) {
				throw ImpossibleResultID("a texture description has no air.arg_type_name");
			}

			uint32_t bindingIndex = airParameterInteger(parameterInfo, infoIdx + 1);
			auto argType = airParameterString(parameterInfo, argTypeIdx + 1);
			auto firstAngle = argType.find('<');
			auto lastAngle = argType.find('>');
			auto comma = argType.find(',');
			auto notWhitespace = comma == argType.npos ? argType.npos : argType.find_first_not_of(" ", comma + 1);
			// the type name has the form class<sample type, access>
			if (firstAngle == argType.npos || comma == argType.npos || lastAngle == argType.npos || notWhitespace == argType.npos
				|| !(firstAngle < comma && comma < notWhitespace && notWhitespace < lastAngle)) {
				throw ImpossibleResultID("malformed texture type name " + std::string(argType));
			}

			std::string_view textureClassName(argType.data(), firstAngle);
			std::string_view sampleTypeName(argType.data() + (firstAngle + 1), comma - (firstAngle + 1));
			std::string_view accessTypeName(argType.data() + notWhitespace, lastAngle - notWhitespace);

			// A texture that is written is a storage image; one that is only sampled or read is a sampled image
			// (read() on it is a fetch).
			if (accessTypeName == "write") {
				accessType = TextureAccessType::Write;
			} else if (accessTypeName == "read_write") {
				accessType = TextureAccessType::ReadWrite;
			}

			// a texture_buffer is a range of a buffer bound as a texel buffer, not an image
			bool texelBuffer = textureClassName == "texture_buffer";
			funcInfo.bindings.push_back(BindingInfo { BindingType::Texture, bindingIndex, internalBindingIndex, accessType, 0, texelBuffer });

			SPIRV::ResultID fakeSampleType = SPIRV::ResultIDInvalid;
			SPIRV::ResultID realSampleType = SPIRV::ResultIDInvalid;
			SPIRV::Dim dimensionality;

			// TODO: find a better way to do this than just matching different strings
			if (sampleTypeName == "half") {
				// WORKAROUND: Vulkan doesn't support 16-bit samplers. not sure why the hell it doesn't when most every other graphics
				//             API does, but it doesn't. so, we work around this by sampling as a 32-bit float and converting to a 16-bit one.
				fakeSampleType = builder.declareType(SPIRV::Type(SPIRV::Type::FloatTag {}, 32));
				realSampleType = builder.declareType(SPIRV::Type(SPIRV::Type::FloatTag {}, 16));
			} else if (sampleTypeName == "float") {
				fakeSampleType = builder.declareType(SPIRV::Type(SPIRV::Type::FloatTag {}, 32));
				realSampleType = fakeSampleType;
			} else if (sampleTypeName == "int" || sampleTypeName == "uint") {
				fakeSampleType = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 32, sampleTypeName == "int"));
				realSampleType = fakeSampleType;
			} else {
				throw ImpossibleResultID("unsupported texture sample type " + std::string(sampleTypeName));
			}

			// depth textures declare the same dimensions as the color ones
			bool isDepth = textureClassName.substr(0, 5) == "depth";
			std::string colorClassName(textureClassName);
			if (isDepth) {
				colorClassName.replace(0, 5, "texture");
			}

			// TODO: same here (find a better way...)
			bool isArrayed = false;
			bool isMultisampled = false;
			if (colorClassName == "texture2d") {
				dimensionality = SPIRV::Dim::e2D;
			} else if (colorClassName == "texture2d_ms") {
				dimensionality = SPIRV::Dim::e2D;
				isMultisampled = true;
			} else if (colorClassName == "texture2d_ms_array") {
				dimensionality = SPIRV::Dim::e2D;
				isArrayed = true;
				isMultisampled = true;
			} else if (colorClassName == "texture2d_array") {
				dimensionality = SPIRV::Dim::e2D;
				isArrayed = true;
			} else if (colorClassName == "texturecube") {
				dimensionality = SPIRV::Dim::eCube;
			} else if (colorClassName == "texturecube_array") {
				dimensionality = SPIRV::Dim::eCube;
				isArrayed = true;
			} else if (colorClassName == "texture1d") {
				dimensionality = SPIRV::Dim::e1D;
			} else if (colorClassName == "texture1d_array") {
				dimensionality = SPIRV::Dim::e1D;
				isArrayed = true;
			} else if (colorClassName == "texture3d") {
				dimensionality = SPIRV::Dim::e3D;
			} else if (texelBuffer) {
				dimensionality = SPIRV::Dim::eBuffer;
			} else {
				// any other class
				throw std::runtime_error(std::string("unsupported texture class ") + std::string(textureClassName));
			}

			auto imageType = builder.declareType(SPIRV::Type(SPIRV::Type::ImageTag {}, fakeSampleType, realSampleType, dimensionality, isDepth ? 1 : 2, isArrayed, isMultisampled, accessType == TextureAccessType::Sample ? 1 : 2, SPIRV::ImageFormat::Unknown));
			auto imagePtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::UniformConstant, imageType, 8));
			auto var = builder.addGlobalVariable(imagePtrType, SPIRV::StorageClass::UniformConstant);
			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::DescriptorSet, { funcInfo.type == FunctionType::Fragment ? 1u : 0u } });
			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Binding, { static_cast<uint32_t>(internalBindingIndex) } });

			builder.referenceGlobalVariable(var);

			_parameterIDs.push_back(var);

			builder.associateExistingResultID(var, reinterpret_cast<uintptr_t>(DynamicLLVM::LLVMGetParam(_function, i)));
			builder.setResultType(var, imagePtrType);

			++internalBindingIndex;
		}
	}

	// now look for samplers and assign bindings for them
	for (size_t i = 0; i < parameterOperands.size(); ++i) {
		auto& parameterOperand = parameterOperands[i];

		// info[0] is the parameter index
		// info[1] is the kind
		// everything after that depends on the kind
		auto parameterInfo = airParameterOperands(parameterOperand);

		auto kind = airParameterKind(parameterInfo);

		if (kind == "air.sampler") {
			// find the location index info
			size_t infoIdx = 0;
			for (; infoIdx < parameterInfo.size(); ++infoIdx) {
				if (parameterInfo[infoIdx] && DynamicLLVM::LLVMIsAMDString(parameterInfo[infoIdx])) {
					auto str = llvmMDStringToStringView(parameterInfo[infoIdx]);

					if (str == "air.location_index") {
						break;
					}
				}
			}

			if (infoIdx >= parameterInfo.size()) {
				// weird, location index info not found
				throw ImpossibleResultID("a texture or sampler description has no air.location_index");
			}

			uint32_t bindingIndex = airParameterInteger(parameterInfo, infoIdx + 1);

			funcInfo.bindings.push_back(BindingInfo { BindingType::Sampler, bindingIndex, internalBindingIndex, /* ignored: */ TextureAccessType::Read, /* ignored: */ SIZE_MAX });

			auto samplerType = builder.declareType(SPIRV::Type(SPIRV::Type::SamplerTag {}));
			auto samplerPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::UniformConstant, samplerType, 8));
			auto var = builder.addGlobalVariable(samplerPtrType, SPIRV::StorageClass::UniformConstant);
			//auto load = builder.encodeLoad(samplerType, var);

			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::DescriptorSet, { funcInfo.type == FunctionType::Fragment ? 1u : 0u } });
			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Binding, { static_cast<uint32_t>(internalBindingIndex) } });

			builder.referenceGlobalVariable(var);

			_parameterIDs.push_back(var);

			builder.associateExistingResultID(var, reinterpret_cast<uintptr_t>(DynamicLLVM::LLVMGetParam(_function, i)));
			builder.setResultType(var, samplerPtrType);

			++internalBindingIndex;
		}
	}

	// declares the Vulkan-side sampler for one embedded (constant) sampler state and returns its variable; when `key` is set, it is the
	// LLVM value the shader refers to it by
	auto declareEmbeddedSampler = [&](uint64_t val, std::optional<uintptr_t> key) {
		// bit positions of each state component (excluding the end point):
		//   S address mode = [0, 3]
		//   T address mode = [3, 6]
		//   R address mode = [6, 9]
		//   magnification filter = [9, 11]
		//   minification filter = [11, 13]
		//   mipmap filter = [13, 15]
		//   uses normalized coords = [15, 16]
		//   compare function = [16, 20]
		//   anisotropy level = [20, 24]
		//   LOD minimum = [24, 40]
		//   LOD maximum = [40, 56]
		//   border color = [56, 58]

		uint8_t sAddrMode = val & 0x07;
		uint8_t tAddrMode = (val >> 3) & 0x07;
		uint8_t rAddrMode = (val >> 6) & 0x07;
		uint8_t magFilter = (val >> 9) & 0x03;
		uint8_t minFilter = (val >> 11) & 0x03;
		uint8_t mipmapFilter = (val >> 13) & 0x03;
		bool usesNormalizedCoords = (val & (1ull << 15)) == 0; // if bit 15 is 0, normalized coords are used; if it's 1, unnormalized coords are used
		uint8_t compareFunction = (val >> 16) & 0x0f;
		uint8_t anisotropyLevel = ((val >> 20) & 0x0f) + 1;
		uint8_t borderColor = (val >> 56) & 0x03;

		// the LOD values are actually floats that have been truncated to half-floats and then bitcast to uint16s,
		// so we need to do a little conversion to get them back as a floats
		uint16_t lodMinU16 = (val >> 24) & 0xffff;
		uint16_t lodMaxU16 = (val >> 40) & 0xffff;

		Float16 lodMinHalf;
		Float16 lodMaxHalf;

		// technically UB, but it's fine
		memcpy(&lodMinHalf, &lodMinU16, sizeof(lodMinHalf));
		memcpy(&lodMaxHalf, &lodMaxU16, sizeof(lodMaxHalf));

		float lodMin = lodMinHalf;
		float lodMax = lodMaxHalf;

		auto embeddedSamplerIndex = funcInfo.embeddedSamplers.size();
		funcInfo.embeddedSamplers.push_back(EmbeddedSampler {
			static_cast<EmbeddedSampler::AddressMode>(sAddrMode),
			static_cast<EmbeddedSampler::AddressMode>(tAddrMode),
			static_cast<EmbeddedSampler::AddressMode>(rAddrMode),
			static_cast<EmbeddedSampler::Filter>(magFilter),
			static_cast<EmbeddedSampler::Filter>(minFilter),
			static_cast<EmbeddedSampler::MipFilter>(mipmapFilter),
			usesNormalizedCoords,
			static_cast<EmbeddedSampler::CompareFunction>(compareFunction),
			anisotropyLevel,
			static_cast<EmbeddedSampler::BorderColor>(borderColor),
			lodMin,
			lodMax,
		});

		funcInfo.bindings.push_back(BindingInfo { BindingType::Sampler, SIZE_MAX, internalBindingIndex, /* ignored: */ TextureAccessType::Read, embeddedSamplerIndex });

		auto samplerType = builder.declareType(SPIRV::Type(SPIRV::Type::SamplerTag {}));
		auto samplerPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::UniformConstant, samplerType, 8));
		auto var = builder.addGlobalVariable(samplerPtrType, SPIRV::StorageClass::UniformConstant);
		//auto load = builder.encodeLoad(samplerType, var);

		builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::DescriptorSet, { funcInfo.type == FunctionType::Fragment ? 1u : 0u } });
		builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Binding, { static_cast<uint32_t>(internalBindingIndex) } });

		builder.referenceGlobalVariable(var);

		_parameterIDs.push_back(var);

		if (key) {
			builder.associateExistingResultID(var, *key);
		}
		builder.setResultType(var, samplerPtrType);

		++internalBindingIndex;
		return var;
	};

	// find global sampler variables
	//
	// unlike Vulkan/SPIR-V, Metal allows you to declare/define samplers within the shader itself. fortunately for us, any such samplers
	// must be constexprs; when compiled, they're saved as global constants and referenced by an "air.sampler_states" metadata node.
	// this means we can find them, extract their values, and push this to the output info for Indium to pass to Vulkan.
	if (auto samplerStatesMD = DynamicLLVM::LLVMGetNamedMetadata(_module.get(), "air.sampler_states", sizeof("air.sampler_states") - 1)) {
		// get the operands
		std::vector<LLVMValueRef> operands(DynamicLLVM::LLVMGetNamedMetadataNumOperands(_module.get(), "air.sampler_states"));
		DynamicLLVM::LLVMGetNamedMetadataOperands(_module.get(), "air.sampler_states", operands.data());

		// each operand contains "air.sampler_state" followed by a reference to a sampler state
		for (const auto& operand: operands) {
			std::vector<LLVMValueRef> suboperands(DynamicLLVM::LLVMGetMDNodeNumOperands(operand));
			DynamicLLVM::LLVMGetMDNodeOperands(operand, suboperands.data());

			auto val = airSamplerStateWord(DynamicLLVM::LLVMGetInitializer(suboperands[1]));

			declareEmbeddedSampler(val, reinterpret_cast<uintptr_t>(suboperands[1]));
		}
	}

	// a constant sampler can also be referenced directly as an integer-to-pointer constant expression holding the same
	// packed state, rather than through a global variable. declare one sampler per distinct expression before the body is translated.
	// the expression is shared by every function in the library, so each function keeps its own variables.
	std::unordered_map<LLVMValueRef, SPIRV::ResultID> constantSamplers;
	std::unordered_set<LLVMValueRef> readSamplerCalls;
	for (LLVMBasicBlockRef bb = DynamicLLVM::LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = DynamicLLVM::LLVMGetNextBasicBlock(bb)) {
		for (LLVMValueRef inst = DynamicLLVM::LLVMGetFirstInstruction(bb); inst != nullptr; inst = DynamicLLVM::LLVMGetNextInstruction(inst)) {
			auto operandCount = DynamicLLVM::LLVMGetNumOperands(inst);
			for (int i = 0; i < operandCount; ++i) {
				auto operand = DynamicLLVM::LLVMGetOperand(inst, i);
				if (DynamicLLVM::LLVMGetValueKind(operand) != LLVMConstantExprValueKind
					|| DynamicLLVM::LLVMGetConstOpcode(operand) != LLVMIntToPtr
					|| DynamicLLVM::LLVMGetPointerAddressSpace(DynamicLLVM::LLVMTypeOf(operand)) != 2
					|| constantSamplers.count(operand))
				{
					continue;
				}
				auto packed = DynamicLLVM::LLVMGetOperand(operand, 0);
				if (!DynamicLLVM::LLVMIsAConstantInt(packed) || DynamicLLVM::LLVMGetIntTypeWidth(DynamicLLVM::LLVMTypeOf(packed)) != 64) {
					throw ImpossibleResultID("constant sampler expression does not hold a 64-bit constant");
				}
				constantSamplers[operand] = declareEmbeddedSampler(DynamicLLVM::LLVMConstIntGetZExtValue(packed), std::nullopt);
			}
		}
	}

	// assign an ID to each block
	bool isFirst = true;
	for (LLVMBasicBlockRef bb = DynamicLLVM::LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = DynamicLLVM::LLVMGetNextBasicBlock(bb)) {
		if (isFirst) {
			isFirst = false;

			// assign the existing ID
			builder.associateExistingResultID(firstLabelID, reinterpret_cast<uintptr_t>(bb));
		} else {
			// assign a new ID
			builder.associateResultID(reinterpret_cast<uintptr_t>(bb));
		}
	}

	//
	// analyze CFG structures
	//
	// SPIR-V's structured control flow needs every conditional branch to name the block its paths merge
	// at (OpSelectionMerge), every loop header to name its merge and continue blocks (OpLoopMerge), and
	// no block to be the merge block of two constructs. The passes run on the module beforehand leave a
	// structurized CFG; recover those blocks from its dominator and post-dominator sets.
	//

	enum class CFGType {
		None = 0,
		Selection,
		Loop,
	};

	struct CFGInfo {
		CFGType type = CFGType::None;
		SPIRV::ResultID mergeBlock = SPIRV::ResultIDInvalid;
		SPIRV::ResultID continueBlock = SPIRV::ResultIDInvalid;
	};

	using BlockSet = std::set<LLVMBasicBlockRef>;

	std::unordered_map<SPIRV::ResultID, CFGInfo> cfgInfos;

	std::vector<LLVMBasicBlockRef> blocks;
	for (LLVMBasicBlockRef bb = DynamicLLVM::LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = DynamicLLVM::LLVMGetNextBasicBlock(bb)) {
		blocks.push_back(bb);
	}
	const BlockSet allBlocks(blocks.begin(), blocks.end());

	auto successorsOf = [](LLVMBasicBlockRef bb) {
		auto terminator = DynamicLLVM::LLVMGetLastInstruction(bb);
		std::vector<LLVMBasicBlockRef> successors(DynamicLLVM::LLVMGetNumSuccessors(terminator));
		for (unsigned i = 0; i < successors.size(); ++i) {
			successors[i] = DynamicLLVM::LLVMGetSuccessor(terminator, i);
		}
		return successors;
	};

	auto intersect = [](const BlockSet& a, const BlockSet& b) {
		BlockSet result;
		std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.begin()));
		return result;
	};

	auto blockID = [&](LLVMBasicBlockRef bb) {
		return builder.lookupResultID(reinterpret_cast<uintptr_t>(bb));
	};

	auto isConditionalBranch = [](LLVMValueRef terminator) {
		return IRIDIUM_IS_BR(DynamicLLVM::LLVMGetInstructionOpcode(terminator)) && DynamicLLVM::LLVMIsConditional(terminator);
	};

	// Solves "set(block) = {block} + the intersection of set(n) over its neighbours n" to a fixed point,
	// starting from the full set. With successors as neighbours (`within` limits which ones count) this
	// gives the post-dominators, with predecessors the dominators.
	auto solve = [&](const BlockSet& universe, auto&& neighboursOf, auto&& isRoot) {
		std::unordered_map<LLVMBasicBlockRef, BlockSet> sets;
		for (auto bb: universe) {
			sets[bb] = isRoot(bb) ? BlockSet { bb } : universe;
		}
		for (bool changed = true; changed;) {
			changed = false;
			for (auto bb: blocks) {
				if (!universe.count(bb) || isRoot(bb)) {
					continue;
				}
				auto next = universe;
				for (auto neighbour: neighboursOf(bb)) {
					next = intersect(next, sets[neighbour]);
				}
				next.insert(bb);
				if (next != sets[bb]) {
					sets[bb] = std::move(next);
					changed = true;
				}
			}
		}
		return sets;
	};

	// The nearest of `candidates` that satisfies `accept`: the one whose own set is largest.
	auto nearest = [](const BlockSet& candidates, std::unordered_map<LLVMBasicBlockRef, BlockSet>& sets, auto&& accept) {
		LLVMBasicBlockRef result = nullptr;
		for (auto candidate: candidates) {
			if (accept(candidate) && (!result || sets[candidate].size() > sets[result].size())) {
				result = candidate;
			}
		}
		return result;
	};

	std::unordered_map<LLVMBasicBlockRef, std::vector<LLVMBasicBlockRef>> predecessors;
	for (auto bb: blocks) {
		for (auto successor: successorsOf(bb)) {
			predecessors[successor].push_back(bb);
		}
	}

	auto postDominators = solve(allBlocks, successorsOf, [&](LLVMBasicBlockRef bb) { return successorsOf(bb).empty(); });
	auto dominators = solve(allBlocks, [&](LLVMBasicBlockRef bb) { return predecessors[bb]; }, [&](LLVMBasicBlockRef bb) { return bb == blocks.front(); });

	// a selection merges at its immediate post-dominator
	for (auto bb: blocks) {
		if (!isConditionalBranch(DynamicLLVM::LLVMGetLastInstruction(bb))) {
			continue;
		}
		auto merge = nearest(postDominators[bb], postDominators, [&](LLVMBasicBlockRef candidate) { return candidate != bb; });
		if (merge) {
			cfgInfos[blockID(bb)] = CFGInfo { CFGType::Selection, blockID(merge) };
		}
	}

	// A back edge (latch -> header, where the header dominates the latch) makes a loop. Its merge is the
	// header's nearest post-dominator outside the loop. The latch's branch moves into a new continue block,
	// because the continue target has to be a block of its own that only branches back to the header.
	std::unordered_map<LLVMBasicBlockRef, SPIRV::ResultID> continueBlocks;
	// A loop header whose branch is a selection within the loop cannot also declare it (it declares the
	// loop), so it branches to a new block that does: header -> {that block, the selection's merge}.
	std::unordered_map<LLVMBasicBlockRef, std::pair<SPIRV::ResultID, SPIRV::ResultID>> headerSelections;

	for (auto latch: blocks) {
		for (auto header: successorsOf(latch)) {
			if (!dominators[latch].count(header)) {
				continue;
			}

			BlockSet loop { header, latch };
			std::vector<LLVMBasicBlockRef> pending;
			if (latch != header) {
				pending.push_back(latch);
			}
			while (!pending.empty()) {
				auto block = pending.back();
				pending.pop_back();
				for (auto predecessor: predecessors[block]) {
					if (loop.insert(predecessor).second && predecessor != header) {
						pending.push_back(predecessor);
					}
				}
			}

			auto merge = nearest(postDominators[header], postDominators, [&](LLVMBasicBlockRef candidate) { return !loop.count(candidate); });
			if (!merge) {
				throw ImpossibleResultID("loop has no merge block");
			}

			auto headerID = blockID(header);
			auto existing = cfgInfos.find(headerID);
			if (existing != cfgInfos.end() && existing->second.type == CFGType::Loop) {
				throw ImpossibleResultID("loop has more than one back edge");
			}

			auto continueID = builder.reserveResultID();
			continueBlocks[latch] = continueID;
			cfgInfos[headerID] = CFGInfo { CFGType::Loop, blockID(merge), continueID };

			// Within the loop, paths that leave it or go back to the header end the enclosing construct,
			// so they do not count when finding where a selection inside the loop merges. Post-dominators
			// over the loop body, with the latch as its only exit, give those merges.
			auto loopSuccessors = [&](LLVMBasicBlockRef bb) {
				std::vector<LLVMBasicBlockRef> result;
				for (auto successor: successorsOf(bb)) {
					if (loop.count(successor) && successor != header) {
						result.push_back(successor);
					}
				}
				return result;
			};
			auto local = solve(loop, loopSuccessors, [&](LLVMBasicBlockRef bb) { return bb == latch; });
			auto innerMerge = [&](LLVMBasicBlockRef block) {
				return nearest(local[block], local, [&](LLVMBasicBlockRef candidate) {
					return candidate != block && candidate != header && local[candidate] != loop;
				});
			};

			auto headerTerminator = DynamicLLVM::LLVMGetLastInstruction(header);
			if (header != latch && isConditionalBranch(headerTerminator) &&
				loop.count(DynamicLLVM::LLVMGetSuccessor(headerTerminator, 0)) && loop.count(DynamicLLVM::LLVMGetSuccessor(headerTerminator, 1))) {
				auto merge = innerMerge(header);
				if (!merge) {
					throw ImpossibleResultID("selection in a loop header has no merge block");
				}
				headerSelections[header] = { builder.reserveResultID(), blockID(merge) };
			}

			for (auto block: loop) {
				auto found = cfgInfos.find(blockID(block));
				if (found == cfgInfos.end() || found->second.type != CFGType::Selection) {
					continue;
				}
				if (block == latch) {
					// the latch's conditional branch continues or leaves the loop; that needs no selection
					cfgInfos.erase(found);
					continue;
				}
				if (auto merge = innerMerge(block)) {
					found->second.mergeBlock = blockID(merge);
				}
			}
		}
	}

	// Two constructs cannot share a merge block. When a selection's merge is already taken (it leaves an
	// enclosing construct), give it a new merge block that is never reached instead. Outer constructs,
	// whose headers have fewer dominators, keep the real block.
	std::vector<SPIRV::ResultID> unreachableMerges;
	{
		std::set<SPIRV::ResultID> claimedMerges;
		auto headers = blocks;
		std::stable_sort(headers.begin(), headers.end(), [&](LLVMBasicBlockRef a, LLVMBasicBlockRef b) {
			return dominators[a].size() < dominators[b].size();
		});
		for (auto bb: headers) {
			auto found = cfgInfos.find(blockID(bb));
			if (found == cfgInfos.end()) {
				continue;
			}
			if (!claimedMerges.insert(found->second.mergeBlock).second) {
				auto id = builder.reserveResultID();
				found->second.mergeBlock = id;
				unreachableMerges.push_back(id);
			}
		}
	}

	// SPIR-V requires a value's definition to come before its uses in block order (except through PHIs),
	// and AIR's block order need not have that property, so emit the blocks in reverse post-order.
	{
		BlockSet visited;
		std::vector<LLVMBasicBlockRef> postOrder;
		std::vector<std::pair<LLVMBasicBlockRef, size_t>> stack { { blocks.front(), 0 } };
		visited.insert(blocks.front());
		while (!stack.empty()) {
			auto& [block, next] = stack.back();
			auto successors = successorsOf(block);
			if (next < successors.size()) {
				auto successor = successors[next++];
				if (visited.insert(successor).second) {
					stack.push_back({ successor, 0 });
				}
			} else {
				postOrder.push_back(block);
				stack.pop_back();
			}
		}
		std::vector<LLVMBasicBlockRef> ordered(postOrder.rbegin(), postOrder.rend());
		for (auto bb: blocks) {
			if (!visited.count(bb)) {
				ordered.push_back(bb);
			}
		}
		blocks = std::move(ordered);
	}

	// A PHI's value can be used across a back edge, before the PHI itself is emitted, so give every PHI
	// its result ID up front.
	for (auto bb: blocks) {
		for (LLVMValueRef inst = DynamicLLVM::LLVMGetFirstInstruction(bb); inst != nullptr; inst = DynamicLLVM::LLVMGetNextInstruction(inst)) {
			if (DynamicLLVM::LLVMGetInstructionOpcode(inst) == LLVMPHI) {
				auto id = builder.associateResultID(reinterpret_cast<uintptr_t>(inst));
				builder.setResultType(id, llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(inst)));
			}
		}
	}

	//
	// translate instructions
	//
	isFirst = true;
	for (auto bb: blocks) {
		const bool entryBlock = isFirst;
		if (isFirst) {
			isFirst = false;

			// we already have a label inserted
		} else {
			// insert a new label
			builder.insertLabel(builder.lookupResultID(reinterpret_cast<uintptr_t>(bb)));
		}

		// set once an instruction ends the SPIR-V block (OpKill); the rest of the AIR block is dead
		bool blockTerminated = false;

		std::vector<LLVMValueRef> blockInstructions;
		if (entryBlock) {
			blockInstructions = initializerInstructions;
		}
		for (LLVMValueRef inst = DynamicLLVM::LLVMGetFirstInstruction(bb); inst != nullptr; inst = DynamicLLVM::LLVMGetNextInstruction(inst)) {
			blockInstructions.push_back(inst);
		}

		for (LLVMValueRef inst: blockInstructions) {
			if (blockTerminated) {
				break;
			}
			auto opcode = DynamicLLVM::LLVMGetInstructionOpcode(inst);

			switch (opcode) {
				case LLVMZExt: {
					auto source = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);
					auto argument = llvmValueToResultID(builder, source);
					auto sourceTypeInst = *builder.reverseLookupType(llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(source)));
					auto sourceComponent = sourceTypeInst.backingType == SPIRV::Type::BackingType::Vector ? *builder.reverseLookupType(sourceTypeInst.targetType) : sourceTypeInst;

					// AIR's i1 is a SPIR-V boolean, which OpUConvert does not take
					auto resID = sourceComponent.backingType == SPIRV::Type::BackingType::Boolean
						? builder.encodeOp(SPIRV::Opcode::Select, type, { argument, integerConstantOfType(builder, type, 1), integerConstantOfType(builder, type, 0) })
						: encodeUnsignedResultOp(builder, SPIRV::Opcode::UConvert, type, { argument });

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMSExt:
				case LLVMTrunc:
				case LLVMFPExt:
				case LLVMFPToSI:
				case LLVMFPToUI:
				case LLVMSIToFP:
				case LLVMUIToFP: {
					auto source = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto argument = llvmValueToResultID(builder, source);
					auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(inst));
					SPIRV::ResultID resID = SPIRV::ResultIDInvalid;

					switch (opcode) {
						case LLVMSExt: resID = builder.encodeOp(SPIRV::Opcode::SConvert, type, { argument }); break;
						case LLVMTrunc: resID = encodeUnsignedResultOp(builder, SPIRV::Opcode::UConvert, type, { argument }); break;
						case LLVMFPExt: resID = builder.encodeFConvert(type, argument); break;
						case LLVMFPToSI: resID = builder.encodeOp(SPIRV::Opcode::ConvertFToS, type, { argument }); break;
						case LLVMFPToUI: resID = encodeUnsignedResultOp(builder, SPIRV::Opcode::ConvertFToU, type, { argument }); break;
						case LLVMSIToFP: resID = builder.encodeOp(SPIRV::Opcode::ConvertSToF, type, { argument }); break;
						case LLVMUIToFP: {
							auto unsignedType = integerTypeWithSign(builder, llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(source)), false);
							resID = builder.encodeOp(SPIRV::Opcode::ConvertUToF, type, { builder.encodeBitcast(unsignedType, argument) });
						} break;
						default: break;
					}

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMAlloca: {
					// a local variable that the passes run on the module could not turn into SSA values
					auto count = DynamicLLVM::LLVMGetOperand(inst, 0);
					if (!DynamicLLVM::LLVMIsAConstantInt(count) || DynamicLLVM::LLVMConstIntGetZExtValue(count) != 1) {
						throw ImpossibleResultID("alloca of a dynamic or multiple-element size");
					}
					LayoutFreeScope layoutFree(true);
					auto valueType = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetAllocatedType(inst));
					auto pointerType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Function, valueType, 8));
					auto resID = builder.addFunctionVariable(pointerType);
					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
				} break;

				// TODO: look into using physical addressing instead of logical addressing, which should simplify the translation process
				//       (since i *think* that's the same addressing mode used by Metal code)

				case LLVMGetElementPtr: {
					// Metal declares packed_T3 elements as 3 * sizeof(T) bytes, while a 3-lane vector,
					// alone or inside an array or struct, covers its allocation size. Which one such a GEP
					// means is unknown (reinterpreting packed_float3* as float3* also produces it), so
					// refuse to guess. Bases reached through another GEP, phi or select are not seen.
					auto packed = _packed3Buffers.find(DynamicLLVM::LLVMGetOperand(inst, 0));
					auto first = DynamicLLVM::LLVMGetNumOperands(inst) > 1 ? DynamicLLVM::LLVMGetOperand(inst, 1) : nullptr;
					if (packed != _packed3Buffers.end() && first) {
						auto sourceType = DynamicLLVM::LLVMGetGEPSourceElementType(inst);
						bool firstIsZero = DynamicLLVM::LLVMIsAConstantInt(first) && DynamicLLVM::LLVMConstIntGetZExtValue(first) == 0;
						// stepping within the first element of a 3-lane vector is unambiguous
						auto vector = isThreeLaneVector(sourceType) && firstIsZero ? nullptr : threeLaneVectorIn(sourceType);
						if (vector) {
							auto layout = *builder.reverseLookupType(llvmTypeToSPIRVType(builder, vector));
							throw ImpossibleResultID(packed->second + " is indexed through a 3-lane vector (" + std::to_string(allocationSize(layout)) +
								"-byte stride); refusing to guess which layout is meant, please report this module");
						}
					}
					llvmGEPToResultID(builder, inst);
				} break;

				case LLVMLoad: {
					auto alignment = DynamicLLVM::LLVMGetAlignment(inst);
					auto ptr = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto op = llvmValueToResultID(builder, ptr);
					// A pointer operand's recorded type is what carries the storage
					// class the re-type below copies, and it does not miss on any
					// AIR in the corpus: every producer of a pointer calls
					// setResultType (a parameter's setup load, a getelementptr, a
					// "bitcast ptr to ptr"), and llvmValueToResultID's constant
					// paths cannot produce one at all. 82 loads and 6 stores across
					// the nine fixtures, no misses. There is also nothing to
					// inherit if it ever did: the zeroed storage an unchecked
					// operator* hands back reads as UniformConstant, a read-only
					// class, which is how a store ends up with "OpStore ... storage
					// class is read-only". Name the operand rather than guess.
					auto maybeOpType = builder.reverseLookupType(builder.lookupResultType(op));
					if (!maybeOpType) {
						throw ImpossibleResultID("load's pointer operand has no recorded result type");
					}
					auto opType = *maybeOpType;
					bool logical = isLogicalStorageClass(opType.pointerStorageClass);
					// an address in device or constant memory, see physicalPointerType
					bool address = opType.pointerStorageClass == SPIRV::StorageClass::PhysicalStorageBuffer
						&& DynamicLLVM::LLVMGetTypeKind(targetType) == LLVMPointerTypeKind;
					auto type = llvmTypeToSPIRVType(builder, targetType, address);
					if (address) {
						alignment = addressAlignment(alignment);
					}

					if (logical) {
						// A local variable or global constant cannot be re-typed: its pointer has no address
						// to reinterpret, so the load has to read the type the object was declared with.
						LayoutFreeScope layoutFree(true);
						type = llvmTypeToSPIRVType(builder, targetType);
						if (type != opType.targetType) {
							auto stepped = descendToFirstElement(builder, op, type);
							if (stepped == op) {
								throw ImpossibleResultID("load reinterprets a local or constant object");
							}
							op = stepped;
						}
						alignment = 0;
					} else {
						// AIR's pointers are opaque, so the operand's pointee says nothing about what is loaded (a
						// device pointer's is always uint, see physicalPointerType). The load's own result type is
						// the one place the intended pointee survives, so re-type the pointer to it.
						auto loadPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, opType.pointerStorageClass, type, 8));

						if (loadPtrType != builder.lookupResultType(op)) {
							auto casted = builder.encodeBitcast(loadPtrType, op);
							builder.setResultType(casted, loadPtrType);
							op = casted;
						}
					}

					auto resID = builder.encodeLoad(type, op, alignment);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMStore: {
					auto alignment = DynamicLLVM::LLVMGetAlignment(inst);
					auto llval = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto llptr = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto val = llvmValueToResultID(builder, llval);
					auto ptr = llvmValueToResultID(builder, llptr);

					// As for a load, the pointer is re-typed to what is stored through it: the stored value's type,
					// taken from its LLVM type because a constant operand never gets a SPIR-V type recorded. The
					// storage class comes from the operand's recorded type (see the LLVMLoad case).
					auto maybeOpType = builder.reverseLookupType(builder.lookupResultType(ptr));
					if (!maybeOpType) {
						throw ImpossibleResultID("store's pointer operand has no recorded result type");
					}
					auto opType = *maybeOpType;
					bool logical = isLogicalStorageClass(opType.pointerStorageClass);
					bool address = opType.pointerStorageClass == SPIRV::StorageClass::PhysicalStorageBuffer
						&& DynamicLLVM::LLVMGetTypeKind(DynamicLLVM::LLVMTypeOf(llval)) == LLVMPointerTypeKind;
					LayoutFreeScope layoutFree(logical);
					auto valueType = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(llval), address);
					auto storePtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, opType.pointerStorageClass, valueType, 8));
					if (address) {
						alignment = addressAlignment(alignment);
					}
					if (address && builder.lookupResultType(val) != valueType) {
						// a variable, texture or sampler has no address to store
						throw ImpossibleResultID("store of a pointer that is not a device or constant address into device memory");
					}

					if (logical) {
						// see the LLVMLoad case
						if (storePtrType != builder.lookupResultType(ptr)) {
							ptr = descendToFirstElement(builder, ptr, builder.reverseLookupType(storePtrType)->targetType);
							if (storePtrType != builder.lookupResultType(ptr)) {
								throw ImpossibleResultID("store reinterprets a local object");
							}
						}
						alignment = 0;
					} else if (storePtrType != builder.lookupResultType(ptr)) {
						auto casted = builder.encodeBitcast(storePtrType, ptr);
						builder.setResultType(casted, storePtrType);
						ptr = casted;
					}

					builder.encodeStore(ptr, val, alignment);
				} break;

				case LLVMCall: {
					auto target = DynamicLLVM::LLVMGetCalledValue(inst);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);

					size_t len = 0;
					auto rawName = DynamicLLVM::LLVMGetValueName2(target, &len);
					std::string_view name;

					if (rawName) {
						name = std::string_view(rawName, len);
					}

					SPIRV::ResultID resID = SPIRV::ResultIDInvalid;

					if (name.substr(0, 14) == "llvm.lifetime.") {
						// the lifetime markers of local variables carry nothing SPIR-V needs
						break;
					}

					if (name == "air.get_read_sampler") {
						// the sampler that read() uses; it has no value of its own (see translateAirTexture)
						readSamplerCalls.insert(inst);
						break;
					}

					auto type = llvmTypeToSPIRVType(builder, targetType);

					if (name == "air.is_function_constant_defined") {
						// the argument is the constant's variable, possibly through a pointer cast
						auto constant = DynamicLLVM::LLVMGetOperand(inst, 0);
						if (DynamicLLVM::LLVMGetValueKind(constant) == LLVMConstantExprValueKind) {
							constant = DynamicLLVM::LLVMGetOperand(constant, 0);
						}
						auto defined = functionConstantDefined.find(constant);
						if (defined == functionConstantDefined.end()) {
							throw ImpossibleResultID("air.is_function_constant_defined of something that is not a function constant");
						}
						resID = defined->second;
					} else if (name.compare(0, 41, "air.normalize_function_constant_predicate") == 0) {
						// a predicate is any non-zero byte; the constants this function reads are 0 or 1 already
						resID = llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 0));
					} else if (name == "air.convert.f.v2f32.u.v2i32") {
						auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);

						resID = builder.encodeConvertUToF(type, llvmValueToResultID(builder, arg));
					} else if (name == "air.wg.barrier") {
						// threadgroup_barrier(flags): every thread of the group arrives before any continues, and the
						// memory the flags name is made visible to the group
						auto flags = DynamicLLVM::LLVMGetOperand(inst, 0);
						if (DynamicLLVM::LLVMGetNumOperands(inst) < 2 || !DynamicLLVM::LLVMIsAConstantInt(flags)) {
							throw ImpossibleResultID("threadgroup barrier without constant memory flags");
						}
						auto memFlags = DynamicLLVM::LLVMConstIntGetZExtValue(flags);
						// SPIR-V memory semantics: AcquireRelease 0x8, UniformMemory 0x40, WorkgroupMemory 0x100, ImageMemory 0x800
						uint32_t semantics = 0x8;
						semantics |= (memFlags & 1) ? 0x40 : 0;
						semantics |= (memFlags & 2) ? 0x100 : 0;
						semantics |= (memFlags & 4) ? 0x800 : 0;
						// scopes: Device 1, Workgroup 2
						builder.encodeOpWithoutResult(SPIRV::Opcode::ControlBarrier, {
							builder.declareConstantScalar<uint32_t>(2),
							builder.declareConstantScalar<uint32_t>((memFlags & 1) ? 1 : 2),
							builder.declareConstantScalar<uint32_t>(semantics),
						});
					} else if (name == "air.atomic.fence") {
						// atomic_thread_fence(flags, order, scope): memory the flags name is ordered for the threads of the scope
						if (DynamicLLVM::LLVMGetNumOperands(inst) < 4) {
							throw ImpossibleResultID("atomic fence has too few arguments");
						}
						auto flags = DynamicLLVM::LLVMGetOperand(inst, 0);
						auto order = DynamicLLVM::LLVMGetOperand(inst, 1);
						auto scope = DynamicLLVM::LLVMGetOperand(inst, 2);
						if (!DynamicLLVM::LLVMIsAConstantInt(flags) || !DynamicLLVM::LLVMIsAConstantInt(order) || !DynamicLLVM::LLVMIsAConstantInt(scope)) {
							throw ImpossibleResultID("atomic fence with a flags, order or scope that is not constant");
						}
						auto memFlags = DynamicLLVM::LLVMConstIntGetZExtValue(flags);
						auto airScope = DynamicLLVM::LLVMConstIntGetZExtValue(scope);
						if (airScope != 1 && airScope != 2) {
							throw ImpossibleResultID("atomic fence with a scope other than threadgroup or device");
						}
						// the same memory classes as a threadgroup barrier (see air.wg.barrier); a relaxed fence orders nothing
						uint32_t semantics = DynamicLLVM::LLVMConstIntGetZExtValue(order) == 0 ? 0 : 0x8;
						if (semantics) {
							semantics |= (memFlags & 1) ? 0x40 : 0;
							semantics |= (memFlags & 2) ? 0x100 : 0;
							semantics |= (memFlags & 4) ? 0x800 : 0;
						}
						// SPIR-V scopes: Device 1, Workgroup 2
						builder.encodeOpWithoutResult(SPIRV::Opcode::MemoryBarrier, {
							builder.declareConstantScalar<uint32_t>(airScope == 2 ? 1 : 2),
							builder.declareConstantScalar<uint32_t>(semantics),
						});
					} else if (name == "air.simdgroup.barrier") {
						// simdgroup_barrier(flags, ...): the threads of the subgroup arrive before any continues; the memory
						// the flags name is made visible to them
						auto flags = DynamicLLVM::LLVMGetNumOperands(inst) < 2 ? nullptr : DynamicLLVM::LLVMGetOperand(inst, 0);
						if (!flags || !DynamicLLVM::LLVMIsAConstantInt(flags)) {
							throw ImpossibleResultID("simdgroup barrier without constant memory flags");
						}
						auto memFlags = DynamicLLVM::LLVMConstIntGetZExtValue(flags);
						// the memory classes of a threadgroup barrier (see air.wg.barrier); no flags order no memory
						uint32_t semantics = memFlags ? 0x8 : 0;
						semantics |= (memFlags & 1) ? 0x40 : 0;
						semantics |= (memFlags & 2) ? 0x100 : 0;
						semantics |= (memFlags & 4) ? 0x800 : 0;
						// scope: Subgroup 3
						builder.encodeOpWithoutResult(SPIRV::Opcode::ControlBarrier, {
							builder.declareConstantScalar<uint32_t>(3),
							builder.declareConstantScalar<uint32_t>(3),
							builder.declareConstantScalar<uint32_t>(semantics),
						});
					} else if (translateAirSimd(builder, inst, name, type, resID)) {
						// handled
					} else if (translateAirAtomic(builder, inst, name, type, resID)) {
						// handled
					} else if (name == "air.discard_fragment") {
						builder.encodeOpWithoutResult(SPIRV::Opcode::Kill, {});
						blockTerminated = true;
					} else if (translateAirTexture(builder, inst, name, type, funcInfo.type, [&](LLVMValueRef sampler) {
						auto constantSampler = constantSamplers.find(sampler);
						return constantSampler != constantSamplers.end() ? constantSampler->second : llvmValueToResultID(builder, sampler);
					}, [&](LLVMValueRef sampler) {
						return readSamplerCalls.count(sampler) != 0;
					}, resID)) {
						// handled
					} else if (isFloatWidthConversion(name)) {
						auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);

						resID = builder.encodeFConvert(type, llvmValueToResultID(builder, arg));
					} else if (name == "air.dot.v3f32" || name == "air.dot.v4f32") {
						auto operand1 = DynamicLLVM::LLVMGetOperand(inst, 0);
						auto operand2 = DynamicLLVM::LLVMGetOperand(inst, 1);

						resID = builder.encodeArithBinop(SPIRV::Opcode::Dot, type, llvmValueToResultID(builder, operand1), llvmValueToResultID(builder, operand2));
					} else if (name == "air.fast_rsqrt.f32") {
						auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);

						resID = builder.encodeInverseSqrt(type, llvmValueToResultID(builder, arg));
					} else if (name == "air.fast_saturate.f32") {
						auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);

						resID = builder.encodeFClamp(type, llvmValueToResultID(builder, arg), builder.declareConstantScalar<float>(0), builder.declareConstantScalar<float>(1));
					} else if (name == "air.fast_pow.f32") {
						auto base = DynamicLLVM::LLVMGetOperand(inst, 0);
						auto exponent = DynamicLLVM::LLVMGetOperand(inst, 1);

						resID = builder.encodePow(type, llvmValueToResultID(builder, base), llvmValueToResultID(builder, exponent));
					} else if (name == "air.fast_sqrt.f32") {
						auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);

						resID = builder.encodeSqrt(type, llvmValueToResultID(builder, arg));
					} else if (name == "air.fast_fmax.f32") {
						auto operand1 = DynamicLLVM::LLVMGetOperand(inst, 0);
						auto operand2 = DynamicLLVM::LLVMGetOperand(inst, 1);

						resID = builder.encodeFMax(type, llvmValueToResultID(builder, operand1), llvmValueToResultID(builder, operand2));
					} else if (translateAirIntrinsic(builder, inst, name, type, resID)) {
						// handled
					} else {
						 throw std::runtime_error(std::string("Unsupported function call (name = ") + std::string(name) + ")");
					}

					if (resID != SPIRV::ResultIDInvalid) {
						builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
						builder.setResultType(resID, type);
					}
				} break;

				case LLVMAdd:
				case LLVMSub:
				case LLVMMul:
				case LLVMUDiv:
				case LLVMSDiv:
				case LLVMURem:
				case LLVMSRem:
				case LLVMShl:
				case LLVMLShr:
				case LLVMAShr:
				case LLVMAnd:
				case LLVMOr:
				case LLVMXor: {
					auto lhs = llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 0));
					auto rhs = llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 1));
					auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(inst));
					auto typeInst = *builder.reverseLookupType(type);
					auto component = typeInst.backingType == SPIRV::Type::BackingType::Vector ? *builder.reverseLookupType(typeInst.targetType) : typeInst;
					SPIRV::ResultID resID = SPIRV::ResultIDInvalid;

					if (component.backingType == SPIRV::Type::BackingType::Boolean) {
						// bitwise operations on i1 are logical ones on SPIR-V booleans
						SPIRV::Opcode logical;
						switch (opcode) {
							case LLVMAnd: logical = SPIRV::Opcode::LogicalAnd; break;
							case LLVMOr: logical = SPIRV::Opcode::LogicalOr; break;
							case LLVMXor: logical = SPIRV::Opcode::LogicalNotEqual; break;
							default: throw ImpossibleResultID("arithmetic on booleans");
						}
						resID = builder.encodeOp(logical, type, { lhs, rhs });
					} else {
						switch (opcode) {
							case LLVMAdd: resID = builder.encodeOp(SPIRV::Opcode::IAdd, type, { lhs, rhs }); break;
							case LLVMSub: resID = builder.encodeOp(SPIRV::Opcode::ISub, type, { lhs, rhs }); break;
							case LLVMMul: resID = builder.encodeOp(SPIRV::Opcode::IMul, type, { lhs, rhs }); break;
							case LLVMSDiv: resID = builder.encodeOp(SPIRV::Opcode::SDiv, type, { lhs, rhs }); break;
							case LLVMSRem: resID = builder.encodeOp(SPIRV::Opcode::SRem, type, { lhs, rhs }); break;
							case LLVMShl: resID = builder.encodeOp(SPIRV::Opcode::ShiftLeftLogical, type, { lhs, rhs }); break;
							case LLVMLShr: resID = builder.encodeOp(SPIRV::Opcode::ShiftRightLogical, type, { lhs, rhs }); break;
							case LLVMAShr: resID = builder.encodeOp(SPIRV::Opcode::ShiftRightArithmetic, type, { lhs, rhs }); break;
							case LLVMAnd: resID = builder.encodeOp(SPIRV::Opcode::BitwiseAnd, type, { lhs, rhs }); break;
							case LLVMOr: resID = builder.encodeOp(SPIRV::Opcode::BitwiseOr, type, { lhs, rhs }); break;
							case LLVMXor: resID = builder.encodeOp(SPIRV::Opcode::BitwiseXor, type, { lhs, rhs }); break;
							case LLVMUDiv:
							case LLVMURem: {
								// OpUDiv and OpUMod take and produce unsigned integers
								auto unsignedType = integerTypeWithSign(builder, type, false);
								resID = encodeUnsignedResultOp(builder, opcode == LLVMUDiv ? SPIRV::Opcode::UDiv : SPIRV::Opcode::UMod, type, {
									builder.encodeBitcast(unsignedType, lhs),
									builder.encodeBitcast(unsignedType, rhs),
								});
							} break;
							default: break;
						}
					}

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMFNeg: {
					auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(inst));
					auto resID = builder.encodeOp(SPIRV::Opcode::FNegate, type, { llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 0)) });
					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMICmp: {
					auto op1 = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto op2 = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(inst));
					SPIRV::Opcode comparison;

					switch (DynamicLLVM::LLVMGetICmpPredicate(inst)) {
						case LLVMIntEQ: comparison = SPIRV::Opcode::IEqual; break;
						case LLVMIntNE: comparison = SPIRV::Opcode::INotEqual; break;
						case LLVMIntUGT: comparison = SPIRV::Opcode::UGreaterThan; break;
						case LLVMIntUGE: comparison = SPIRV::Opcode::UGreaterThanEqual; break;
						case LLVMIntULT: comparison = SPIRV::Opcode::ULessThan; break;
						case LLVMIntULE: comparison = SPIRV::Opcode::ULessThanEqual; break;
						case LLVMIntSGT: comparison = SPIRV::Opcode::SGreaterThan; break;
						case LLVMIntSGE: comparison = SPIRV::Opcode::SGreaterThanEqual; break;
						case LLVMIntSLT: comparison = SPIRV::Opcode::SLessThan; break;
						case LLVMIntSLE: comparison = SPIRV::Opcode::SLessThanEqual; break;
						default:
							throw std::runtime_error("TODO: handle icmp predicate: " + std::to_string(DynamicLLVM::LLVMGetICmpPredicate(inst)));
					}

					auto lhs = llvmValueToResultID(builder, op1);
					auto rhs = llvmValueToResultID(builder, op2);
					auto operandType = *builder.reverseLookupType(llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(op1)));
					auto operandComponent = operandType.backingType == SPIRV::Type::BackingType::Vector ? *builder.reverseLookupType(operandType.targetType) : operandType;
					if (operandComponent.backingType == SPIRV::Type::BackingType::Boolean) {
						// only equality is defined on SPIR-V booleans
						if (comparison != SPIRV::Opcode::IEqual && comparison != SPIRV::Opcode::INotEqual) {
							throw ImpossibleResultID("ordered comparison of booleans");
						}
						comparison = comparison == SPIRV::Opcode::IEqual ? SPIRV::Opcode::LogicalEqual : SPIRV::Opcode::LogicalNotEqual;
					}

					auto resID = builder.encodeOp(comparison, type, { lhs, rhs });

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMSelect: {
					auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(inst));
					auto resID = builder.encodeOp(SPIRV::Opcode::Select, type, {
						llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 0)),
						llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 1)),
						llvmValueToResultID(builder, DynamicLLVM::LLVMGetOperand(inst, 2)),
					});

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMFMul:
				case LLVMFDiv:
				case LLVMFAdd:
				case LLVMFSub:
				case LLVMFRem: {
					auto op1 = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto op2 = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);
					SPIRV::Opcode binop;

					switch (opcode) {
						case LLVMFMul: binop = SPIRV::Opcode::FMul; break;
						case LLVMFDiv: binop = SPIRV::Opcode::FDiv; break;
						case LLVMFAdd: binop = SPIRV::Opcode::FAdd; break;
						case LLVMFSub: binop = SPIRV::Opcode::FSub; break;
						// frem takes the sign of the dividend, as OpFRem does
						case LLVMFRem: binop = SPIRV::Opcode::FRem; break;
						default:
							break;
					}

					auto resID = builder.encodeArithBinop(binop, type, llvmValueToResultID(builder, op1), llvmValueToResultID(builder, op2));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMShuffleVector: {
					auto vec1 = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto vec2 = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					std::vector<uint32_t> components(DynamicLLVM::LLVMGetNumMaskElements(inst));

					for (size_t i = 0; i < components.size(); ++i) {
						auto val = DynamicLLVM::LLVMGetMaskValue(inst, i);
						components[i] = (val == DynamicLLVM::LLVMGetUndefMaskElem()) ? UINT32_MAX : val;
					}

					auto resID = builder.encodeVectorShuffle(type, llvmValueToResultID(builder, vec1), llvmValueToResultID(builder, vec2), components);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMInsertValue: {
					auto composite = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto part = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto llindices = DynamicLLVM::LLVMGetIndices(inst);
					std::vector<uint32_t> indices(llindices, llindices + DynamicLLVM::LLVMGetNumIndices(inst));

					auto resID = builder.encodeCompositeInsert(type, llvmValueToResultID(builder, part), llvmValueToResultID(builder, composite), indices);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMExtractValue: {
					auto composite = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto llindices = DynamicLLVM::LLVMGetIndices(inst);
					std::vector<uint32_t> indices(llindices, llindices + DynamicLLVM::LLVMGetNumIndices(inst));

					auto resID = builder.encodeCompositeExtract(type, llvmValueToResultID(builder, composite), indices);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMInsertElement: {
					auto vector = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto component = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto index = DynamicLLVM::LLVMGetOperand(inst, 2);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto resID = builder.encodeVectorInsertDynamic(type, llvmValueToResultID(builder, vector), llvmValueToResultID(builder, component), llvmValueToResultID(builder, index));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMExtractElement: {
					auto vector = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto index = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto resID = builder.encodeVectorExtractDynamic(type, llvmValueToResultID(builder, vector), llvmValueToResultID(builder, index));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMRet: {
					if (DynamicLLVM::LLVMGetNumOperands(inst) > 0) {
						auto llval = DynamicLLVM::LLVMGetOperand(inst, 0);

						// should be the same as the function return value
						auto type = DynamicLLVM::LLVMTypeOf(llval);
						auto val = llvmValueToResultID(builder, llval);

						if (DynamicLLVM::LLVMGetTypeKind(type) == LLVMStructTypeKind) {
							auto structMemberCount = DynamicLLVM::LLVMGetNumContainedTypes(type);

							for (size_t i = 0; i < structMemberCount; ++i) {
								auto memberType = DynamicLLVM::LLVMStructGetTypeAtIndex(type, i);
								auto type = llvmTypeToSPIRVType(builder, memberType);
								auto elm = builder.encodeCompositeExtract(type, val, { static_cast<uint32_t>(i) });

								if (i == _positionOutputIndex) {
									auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type, 8));
									auto ptr = builder.encodeAccessChain(ptrType, perVertexVar, { builder.declareConstantScalar<int32_t>(0) });
									builder.encodeStore(ptr, elm);
								} else {
									builder.encodeStore(_outputValueIDs[i], elm);
								}
							}
						} else if (_positionOutputIndex == 0) {
							auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, vec4Type, 8));
							auto ptr = builder.encodeAccessChain(ptrType, perVertexVar, { builder.declareConstantScalar<int32_t>(0) });
							builder.encodeStore(ptr, val);
						} else {
							builder.encodeStore(_outputValueIDs[0], val);
						}
					}

					builder.encodeReturn();
				} break;

				case LLVMFCmp: {
					auto op1 = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto op2 = DynamicLLVM::LLVMGetOperand(inst, 1);
					auto predicate = DynamicLLVM::LLVMGetFCmpPredicate(inst);

					// a vector comparison produces a vector of booleans
					auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMTypeOf(inst));
					SPIRV::Opcode comparison;

					switch (predicate) {
						case LLVMRealOEQ: comparison = SPIRV::Opcode::FOrdEqual; break;
						case LLVMRealOGT: comparison = SPIRV::Opcode::FOrdGreaterThan; break;
						case LLVMRealOGE: comparison = SPIRV::Opcode::FOrdGreaterThanEqual; break;
						case LLVMRealOLT: comparison = SPIRV::Opcode::FOrdLessThan; break;
						case LLVMRealOLE: comparison = SPIRV::Opcode::FOrdLessThanEqual; break;
						case LLVMRealONE: comparison = SPIRV::Opcode::FOrdNotEqual; break;
						case LLVMRealORD: comparison = SPIRV::Opcode::Ordered; break;
						case LLVMRealUNO: comparison = SPIRV::Opcode::Unordered; break;
						case LLVMRealUEQ: comparison = SPIRV::Opcode::FUnordEqual; break;
						case LLVMRealUGT: comparison = SPIRV::Opcode::FUnordGreaterThan; break;
						case LLVMRealUGE: comparison = SPIRV::Opcode::FUnordGreaterThanEqual; break;
						case LLVMRealULT: comparison = SPIRV::Opcode::FUnordLessThan; break;
						case LLVMRealULE: comparison = SPIRV::Opcode::FUnordLessThanEqual; break;
						case LLVMRealUNE: comparison = SPIRV::Opcode::FUnordNotEqual; break;
						default:
							throw std::runtime_error("TODO: handle fcmp predicate: " + std::to_string(predicate));
					}

					auto resID = builder.encodeOp(comparison, type, { llvmValueToResultID(builder, op1), llvmValueToResultID(builder, op2) });

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

#if LLVM_VERSION_MAJOR >= 23
				case LLVMUncondBr:
				case LLVMCondBr: {
#else
				case LLVMBr: {
#endif
					auto loop = cfgInfos.find(builder.lookupResultID(reinterpret_cast<uintptr_t>(bb)));
					if (loop != cfgInfos.end() && loop->second.type == CFGType::Loop) {
						builder.encodeLoopMerge(loop->second.mergeBlock, loop->second.continueBlock);
						auto selection = headerSelections.find(bb);
						if (selection != headerSelections.end()) {
							builder.encodeBranch(selection->second.first);
							builder.insertLabel(selection->second.first);
							builder.encodeSelectionMerge(selection->second.second);
						}
					}

					auto continueBlock = continueBlocks.find(bb);
					if (continueBlock != continueBlocks.end()) {
						builder.encodeBranch(continueBlock->second);
						builder.insertLabel(continueBlock->second);
					}

					if (DynamicLLVM::LLVMIsConditional(inst)) {
						auto condition = DynamicLLVM::LLVMGetCondition(inst);
						auto trueLabel = DynamicLLVM::LLVMBasicBlockAsValue(DynamicLLVM::LLVMGetSuccessor(inst, 0));
						auto falseLabel = DynamicLLVM::LLVMBasicBlockAsValue(DynamicLLVM::LLVMGetSuccessor(inst, 1));

						// TODO: detect more CFG structures

						auto it = cfgInfos.find(builder.lookupResultID(reinterpret_cast<uintptr_t>(bb)));

						if (it != cfgInfos.end() && it->second.type == CFGType::Selection) {
							builder.encodeSelectionMerge(it->second.mergeBlock);
						}

						builder.encodeBranchConditional(llvmValueToResultID(builder, condition), llvmValueToResultID(builder, trueLabel), llvmValueToResultID(builder, falseLabel));
					} else {
						auto label = DynamicLLVM::LLVMBasicBlockAsValue(DynamicLLVM::LLVMGetSuccessor(inst, 0));

						builder.encodeBranch(llvmValueToResultID(builder, label));
					}
				} break;

				case LLVMPHI: {
					auto lltype = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, lltype);

					std::vector<std::pair<SPIRV::ResultID, SPIRV::ResultID>> variablesAndBlocks;
					std::vector<uintptr_t> forwardValues;

					auto opCount = DynamicLLVM::LLVMCountIncoming(inst);
					for (size_t i = 0; i < opCount; ++i) {
						auto variable = DynamicLLVM::LLVMGetIncomingValue(inst, i);
						auto incomingBlock = DynamicLLVM::LLVMGetIncomingBlock(inst, i);
						auto label = llvmValueToResultID(builder, DynamicLLVM::LLVMBasicBlockAsValue(incomingBlock));

						// the edge now leaves from the block the branch was moved to
						auto continueBlock = continueBlocks.find(incomingBlock);
						if (continueBlock != continueBlocks.end()) {
							label = continueBlock->second;
						}
						auto selection = headerSelections.find(incomingBlock);
						if (selection != headerSelections.end()) {
							label = selection->second.first;
						}

						// a value coming in over a back edge may not have been translated yet
						bool forward = builder.lookupResultID(reinterpret_cast<uintptr_t>(variable)) == SPIRV::ResultIDInvalid &&
							DynamicLLVM::LLVMGetValueKind(variable) == LLVMInstructionValueKind;
						variablesAndBlocks.push_back(std::make_pair(forward ? SPIRV::ResultIDInvalid : llvmValueToResultID(builder, variable), label));
						forwardValues.push_back(forward ? reinterpret_cast<uintptr_t>(variable) : 0);
					}

					auto resID = builder.encodePhi(type, variablesAndBlocks, builder.lookupResultID(reinterpret_cast<uintptr_t>(inst)), forwardValues);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMBitCast: {
					auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto argID = llvmValueToResultID(builder, arg);
					auto origType = builder.lookupResultType(argID);

					// AIR bitcasts texture handles as
					// "bitcast ptr addrspace(2) %x to ptr addrspace(2)". With
					// opaque pointers LLVMTypeOf reports the same bare pointer
					// for both sides, so the LLVM type carries no destination
					// pointee to translate, and none is recorded anywhere else
					// in the AIR either. The source operand's SPIR-V type does
					// carry one, so reuse that rather than failing the
					// translation; the bitcast is then inert on types. A
					// consumer that needs a different pointee re-types the
					// pointer itself -- see LLVMLoad.
					auto destinationType = DynamicLLVM::LLVMTypeOf(inst);
					auto origTypeInst = builder.reverseLookupType(origType);
					SPIRV::ResultID type;
					if (DynamicLLVM::LLVMGetTypeKind(destinationType) == LLVMPointerTypeKind
						&& origTypeInst && origTypeInst->backingType == SPIRV::Type::BackingType::Pointer) {
						type = origType;
					} else {
						type = llvmTypeToSPIRVType(builder, destinationType);
					}

					// ensure the resulting pointer storage class is the same as the input
					// pointer storage class. a storage class only exists on a pointer type,
					// so there is nothing to inherit from a non-pointer source, and nothing
					// at all from an operand with no type recorded -- which is every
					// constant, since declareConstantScalar and its siblings never call
					// setResultType. the recovered type then stands on its own. dereferencing
					// the missing lookup anyway did not report it: std::optional's operator*
					// on an empty optional neither throws nor crashes, it reads the zeroed
					// storage, in which pointerStorageClass is 0, i.e. UniformConstant, a
					// read-only class.
					auto typeInst = *builder.reverseLookupType(type);
					if (origTypeInst && origTypeInst->backingType == SPIRV::Type::BackingType::Pointer
						&& typeInst.backingType == SPIRV::Type::BackingType::Pointer) {
						typeInst.pointerStorageClass = origTypeInst->pointerStorageClass;
					}
					auto resultType = builder.declareType(typeInst);

					auto resID = typeInst.backingType == SPIRV::Type::BackingType::Pointer && resultType == origType
						? argID : builder.encodeBitcast(resultType, argID);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, resultType);
				} break;

				case LLVMFPTrunc: {
					auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto lltype = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, lltype);
					auto argID = llvmValueToResultID(builder, arg);

					// TODO: check if this needs to be different (e.g. to use QuantizeToF16 or something like that)
					auto resID = builder.encodeFConvert(type, argID);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMUnreachable: {
					builder.encodeUnreachable();
				} break;

				default:
					throw std::runtime_error("Unhandled instruction opcode: " + std::to_string(opcode));
			}
		}
	}

	for (auto label: unreachableMerges) {
		builder.insertLabel(label);
		builder.encodeUnreachable();
	}

	builder.endFunction();
};

Iridium::AIR::Library::Library(const void* data, size_t size) {
	ByteReader reader(data, size);

	// the header has a minimum length of 88 bytes
	if (size < 88) {
		throw std::runtime_error("Metal library size too small");
	}

	if (reader.readString(4) != "MTLB") {
		throw std::runtime_error("Invalid Metal library magic");
	}

	reader.seek(4);

	auto platformID = reader.readIntegerLE<uint16_t>();
	auto fileMajor = reader.readIntegerLE<uint16_t>();
	auto fileMinor = reader.readIntegerLE<uint16_t>();
	auto libType = reader.readIntegerLE<uint8_t>();
	auto targetOS = reader.readIntegerLE<uint8_t>();
	auto osMajor = reader.readIntegerLE<uint16_t>();
	auto osMinor = reader.readIntegerLE<uint16_t>();
	auto fileSize = reader.readIntegerLE<uint64_t>();
	auto funcListOffset = reader.readIntegerLE<uint64_t>();
	auto funcListSize = reader.readIntegerLE<uint64_t>();
	auto pubMetaOffset = reader.readIntegerLE<uint64_t>();
	auto pubMetaSize = reader.readIntegerLE<uint64_t>();
	auto privMetaOffset = reader.readIntegerLE<uint64_t>();
	auto privMetaSize = reader.readIntegerLE<uint64_t>();
	auto bcOffset = reader.readIntegerLE<uint64_t>();
	auto bcSize = reader.readIntegerLE<uint64_t>();

	reader.seek(funcListOffset);

	auto funcEntryCount = reader.readIntegerLE<uint32_t>();

	for (size_t i = 0; i < funcEntryCount; ++i) {
		auto tagGroupSize = reader.readIntegerLE<uint32_t>();
		// for some reason, the tag group size includes the size itself, but tag sizes do *not* include the tag headers
		auto subreader = reader.subrange(tagGroupSize - 4);

		Function::Type funcType;
		std::string funcName;
		const void* bitcode = nullptr;
		size_t bitcodeSize = 0;
		uint64_t funcBCOffset = 0;

		while (true) {
			auto tagName = subreader.readString(4);

			if (tagName == "ENDT") {
				break;
			}

			auto tagSize = subreader.readIntegerLE<uint16_t>();

			if (tagName == "NAME") {
				funcName = subreader.readVariableString();
			} else if (tagName == "MDSZ") {
				bitcodeSize = subreader.readIntegerLE<uint64_t>();
			} else if (tagName == "TYPE") {
				funcType = static_cast<Function::Type>(subreader.readIntegerLE<uint8_t>());
			} else if (tagName == "OFFT") {
				auto funcPubMetaOffset = subreader.readIntegerLE<uint64_t>();
				auto funcPrivMetaOffset = subreader.readIntegerLE<uint64_t>();
				funcBCOffset = subreader.readIntegerLE<uint64_t>();

				bitcode = static_cast<const char*>(data) + bcOffset + funcBCOffset;
			} else {
				subreader.skip(tagSize);
			}
		}

		if (bitcodeSize == 0 && bitcode != nullptr) {
			const uint8_t* bc = reinterpret_cast<const uint8_t*>(bitcode);
			if (funcBCOffset < bcSize && (bcSize - funcBCOffset) >= 20) {
				if (bc[0] == 0xde && bc[1] == 0xc0 && bc[2] == 0x17 && bc[3] == 0x0b) {
					uint32_t wrappedOffset = *reinterpret_cast<const uint32_t*>(bc + 8);
					uint32_t wrappedSize = *reinterpret_cast<const uint32_t*>(bc + 12);
					bitcodeSize = static_cast<size_t>(wrappedOffset) + static_cast<size_t>(wrappedSize);
				}
			}
			if (bitcodeSize == 0 && funcBCOffset < bcSize) {
				bitcodeSize = bcSize - funcBCOffset;
			}
		}

		_functions.try_emplace(funcName, funcType, funcName, bitcode, bitcodeSize);
	}
};

const Iridium::AIR::Function* Iridium::AIR::Library::getFunction(const std::string& name) const {
	auto it = _functions.find(name);
	if (it == _functions.end()) {
		return nullptr;
	}
	return &it->second;
};

void Iridium::AIR::Library::buildModule(SPIRV::Builder& builder, OutputInfo& outputInfo) {
	builder.requireCapability(SPIRV::Capability::Shader);
	builder.requireCapability(SPIRV::Capability::PhysicalStorageBufferAddresses);
	builder.setAddressingModel(SPIRV::AddressingModel::PhysicalStorageBuffer64);
	builder.setMemoryModel(SPIRV::MemoryModel::GLSL450);
	builder.setVersion(1, 5);

	for (auto& [name, func]: _functions) {
		func.analyze(builder, _outputInfo);
	}

	outputInfo = _outputInfo;
};
