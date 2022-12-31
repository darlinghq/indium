#include <iridium/air.hpp>
#include <iridium/bits.hpp>
#include <iridium/spirv.hpp>
#include <iridium/dynamic-llvm.hpp>

#include <llvm-c/BitReader.h>

namespace DynamicLLVM = Iridium::DynamicLLVM;

// special thanks to https://github.com/YuAo/MetalLibraryArchive for information on the library format

// we use LLVM's C API because it's much more stable than the C++ API and it's good enough for our needs

Iridium::AIR::Function::Function(Type type, const std::string& name, const void* bitcode, size_t bitcodeSize):
	_name(name),
	_type(type)
{
	LLVMModuleRef moduleRef;
	_bitcodeBuffer = LLVMSupport::MemoryBuffer(DynamicLLVM::LLVMCreateMemoryBufferWithMemoryRange(reinterpret_cast<const char*>(bitcode), bitcodeSize, "", false), DynamicLLVM::LLVMDisposeMemoryBuffer);

	if (!_bitcodeBuffer) {
		// TODO
		abort();
	}

	if (DynamicLLVM::LLVMParseBitcode2(_bitcodeBuffer.get(), &moduleRef)) {
		// TODO
		abort();
	}

	_module = LLVMSupport::Module(moduleRef, DynamicLLVM::LLVMDisposeModule);

	_function = DynamicLLVM::LLVMGetNamedFunction(_module.get(), _name.c_str());

	if (!_function) {
		// TODO
		abort();
	}
};

// TEST
#include <iostream>

class ImpossibleResultID: public std::exception {};

static Iridium::SPIRV::ResultID llvmTypeToSPIRVType(Iridium::SPIRV::Builder& builder, LLVMTypeRef llvmType) {
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
					auto type = llvmTypeToSPIRVType(builder, typeAtIndex);
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
				auto type = llvmTypeToSPIRVType(builder, typeAtIndex);
				auto typeInst = *builder.reverseLookupType(type);
				auto alignmentToUse = (typeInst.alignment < structAlignment) ? typeInst.alignment : structAlignment;
				// align the offset
				offset = (offset + (alignmentToUse - 1)) & ~(alignmentToUse - 1);
				members.push_back(Type::Member { type, offset, {} });
				// increment the offset
				offset += typeInst.size;
			}
			// align the final offset to determine the structure size
			offset = (offset + (structAlignment - 1)) & ~(structAlignment - 1);
			return builder.declareType(Type(Type::StructureTag {}, members, offset, structAlignment));
		} break;

		case LLVMPointerTypeKind: {
			StorageClass storageClass = StorageClass::Output;
			// TODO: somehow determine the appropriate storage class
			//auto addrSpace = DynamicLLVM::LLVMGetPointerAddressSpace(llvmType);
			return builder.declareType(Type(Type::PointerTag {}, storageClass, llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetElementType(llvmType)), 8));
		} break;

		case LLVMVectorTypeKind: {
			auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetElementType(llvmType));
			auto typeInst = *builder.reverseLookupType(type);
			auto elmCount = DynamicLLVM::LLVMGetVectorSize(llvmType);
			return builder.declareType(Type(Type::VectorTag {}, elmCount, type, typeInst.size * elmCount, ((elmCount == 3 || elmCount == 4) ? 4 : 2) * typeInst.alignment));
		} break;

		case LLVMArrayTypeKind: {
			auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetElementType(llvmType));
			auto typeInst = *builder.reverseLookupType(type);
			auto elmCount = DynamicLLVM::LLVMGetArrayLength(llvmType);
			return builder.declareType(Type(Type::ArrayTag {}, type, elmCount, typeInst.size * elmCount, typeInst.alignment));
		} break;

		default:
			throw ImpossibleResultID();
	}
};

static std::string_view llvmMDStringToStringView(LLVMValueRef llvmMDString) {
	unsigned int length = 0;
	auto rawStr = DynamicLLVM::LLVMGetMDString(llvmMDString, &length);
	return std::string_view(rawStr, length);
};

static Iridium::SPIRV::ResultID llvmValueToResultID(Iridium::SPIRV::Builder& builder, LLVMValueRef llvmValue) {
	using namespace Iridium::SPIRV;

	auto kind = DynamicLLVM::LLVMGetValueKind(llvmValue);
	auto type = DynamicLLVM::LLVMTypeOf(llvmValue);
	auto typeKind = DynamicLLVM::LLVMGetTypeKind(type);

	{
		ResultID maybe = builder.lookupResultID(reinterpret_cast<uintptr_t>(llvmValue));
		if (maybe != ResultIDInvalid) {
			return maybe;
		}
	}

	switch (kind) {
		case LLVMConstantIntValueKind: {
			auto val = DynamicLLVM::LLVMConstIntGetZExtValue(llvmValue);
			auto width = DynamicLLVM::LLVMGetIntTypeWidth(type );
			// TODO: how to determine if it's signed or not?
			return (width <= 32) ? builder.declareConstantScalar<int32_t>(val) : builder.declareConstantScalar<int64_t>(val);
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
					throw ImpossibleResultID();
			}
		} break;

		case LLVMConstantVectorValueKind: {
			std::vector<ResultID> vals;
			auto count = DynamicLLVM::LLVMGetVectorSize(type);

			for (size_t i = 0; i < count; ++i) {
				auto constant = DynamicLLVM::LLVMGetOperand(llvmValue, i);
				vals.push_back(llvmValueToResultID(builder, constant));
			}

			return builder.declareConstantComposite(llvmTypeToSPIRVType(builder, type), vals);
		} break;

		case LLVMConstantDataVectorValueKind: {
			std::vector<ResultID> vals;

			auto count = DynamicLLVM::LLVMGetVectorSize(type);

			for (size_t i = 0; i < count; ++i) {
				auto constant = DynamicLLVM::LLVMGetElementAsConstant(llvmValue, i);
				vals.push_back(llvmValueToResultID(builder, constant));
			}

			return builder.declareConstantComposite(llvmTypeToSPIRVType(builder, type), vals);
		} break;

		case LLVMUndefValueValueKind: {
			return builder.declareUndefinedValue(llvmTypeToSPIRVType(builder, type));
		} break;

		case LLVMConstantAggregateZeroValueKind: {
			// SPIR-V's concept of 'null' seems to map pretty cleanly to LLVM's zero-initializers
			return builder.declareNullValue(llvmTypeToSPIRVType(builder, type));
		} break;

		case LLVMConstantExprValueKind: {
			switch (DynamicLLVM::LLVMGetConstOpcode(llvmValue)) {
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
					throw ImpossibleResultID();
			}
		} break;

		default:
			throw ImpossibleResultID();
	}
};

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
	auto vec4Type = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 4, floatType, floatTypeInst.size * 4, 16));
	auto u32Type = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 32, false));
	auto uvec3Type = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 3, u32Type, 16, 16));

	SPIRV::ResultID perVertexVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID vertexIndexVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID fragCoordVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID uboPointersVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID computeLocalSizeX = SPIRV::ResultIDInvalid;
	SPIRV::ResultID computeLocalSizeY = SPIRV::ResultIDInvalid;
	SPIRV::ResultID computeLocalSizeZ = SPIRV::ResultIDInvalid;
	SPIRV::ResultID globalInvocationIdVar = SPIRV::ResultIDInvalid;

	auto firstLabelID = builder.beginFunction(funcID.id);

	// within this node, operand 0 refers to the vertex function,
	// operand 1 contains information about the function's return value,
	// and operand 2 contains information about the function's parameters.
	std::vector<LLVMValueRef> rootInfoOperands;

	if (auto vertexMD = DynamicLLVM::LLVMGetNamedMetadata(_module.get(), "air.vertex", sizeof("air.vertex") - 1)) {
		// this is a vertex shader

		funcInfo.type = FunctionType::Vertex;

		builder.addEntryPoint(SPIRV::EntryPoint {
			SPIRV::ExecutionModel::Vertex,
			funcID.id,
			_name,
			{},
		});

		// declare the GLSL PerVertex structure
		auto perVertexStructType = builder.declareType(SPIRV::Type(SPIRV::Type::StructureTag {}, {
			SPIRV::Type::Member {
				vec4Type,
				0,
				{
					SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::Position) } },
				}
			},
			// TODO: declare the rest of the structure
		}, floatTypeInst.size * 4, floatTypeInst.alignment));
		auto perVertexStructPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, perVertexStructType, 8));
		perVertexVar = builder.addGlobalVariable(perVertexStructPtrType, SPIRV::StorageClass::Output);

		builder.addDecoration(perVertexStructType, SPIRV::Decoration { SPIRV::DecorationType::Block });

		builder.referenceGlobalVariable(perVertexVar);

		// declare the GLSL VertexIndex input variable
		vertexIndexVar = builder.addGlobalVariable(ptrIntType, SPIRV::StorageClass::Input);

		builder.addDecoration(vertexIndexVar, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::VertexIndex) } });

		builder.referenceGlobalVariable(vertexIndexVar);

		// get the operands
		std::vector<LLVMValueRef> operands(DynamicLLVM::LLVMGetNamedMetadataNumOperands(_module.get(), "air.vertex"));
		DynamicLLVM::LLVMGetNamedMetadataOperands(_module.get(), "air.vertex", operands.data());

		// operand 0 contains the info for the vertex shader
		auto rootInfoMD = operands[0];

		rootInfoOperands = std::vector<LLVMValueRef>(DynamicLLVM::LLVMGetMDNodeNumOperands(rootInfoMD));
		DynamicLLVM::LLVMGetMDNodeOperands(rootInfoMD, rootInfoOperands.data());
	} else if (auto fragmentMD = DynamicLLVM::LLVMGetNamedMetadata(_module.get(), "air.fragment", sizeof("air.fragment") - 1)) {
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

		// get the operands
		std::vector<LLVMValueRef> operands(DynamicLLVM::LLVMGetNamedMetadataNumOperands(_module.get(), "air.fragment"));
		DynamicLLVM::LLVMGetNamedMetadataOperands(_module.get(), "air.fragment", operands.data());

		// operand 0 contains the info for the fragment shader
		auto rootInfoMD = operands[0];

		rootInfoOperands = std::vector<LLVMValueRef>(DynamicLLVM::LLVMGetMDNodeNumOperands(rootInfoMD));
		DynamicLLVM::LLVMGetMDNodeOperands(rootInfoMD, rootInfoOperands.data());
	} else if (auto kernelMD = DynamicLLVM::LLVMGetNamedMetadata(_module.get(), "air.kernel", sizeof("air.kernel") - 1)) {
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

		// get the operands
		std::vector<LLVMValueRef> operands(DynamicLLVM::LLVMGetNamedMetadataNumOperands(_module.get(), "air.kernel"));
		DynamicLLVM::LLVMGetNamedMetadataOperands(_module.get(), "air.kernel", operands.data());

		// operand 0 contains the info for the compute shader
		auto rootInfoMD = operands[0];

		rootInfoOperands = std::vector<LLVMValueRef>(DynamicLLVM::LLVMGetMDNodeNumOperands(rootInfoMD));
		DynamicLLVM::LLVMGetMDNodeOperands(rootInfoMD, rootInfoOperands.data());
	}

	// TODO: inspect the output of some more compiled shaders; the current code is only valid for
	//       shaders that return simple structures, vectors, or scalars as outputs.

	// SPIR-V doesn't allow structures to have a storage class of "output", so
	// if the function returns a structure, we need to separate the components
	// into separate output variables.

	auto llfuncType = DynamicLLVM::LLVMGetElementType(DynamicLLVM::LLVMTypeOf(_function));
	auto funcRetType = DynamicLLVM::LLVMGetReturnType(llfuncType);
	std::vector<LLVMTypeRef> funcParamTypes(DynamicLLVM::LLVMCountParamTypes(llfuncType));
	DynamicLLVM::LLVMGetParamTypes(llfuncType, funcParamTypes.data());

	std::vector<LLVMValueRef> returnValueOperands(DynamicLLVM::LLVMGetMDNodeNumOperands(rootInfoOperands[1]));
	DynamicLLVM::LLVMGetMDNodeOperands(rootInfoOperands[1], returnValueOperands.data());

	if (DynamicLLVM::LLVMGetTypeKind(funcRetType) == LLVMStructTypeKind) {
		// analyze return value and mark special values (like the position)
		uint32_t location = 0;
		for (size_t i = 0; i < returnValueOperands.size(); ++i) {
			auto& returnValueOperand = returnValueOperands[i];

			std::vector<LLVMValueRef> returnValueMemberInfo(DynamicLLVM::LLVMGetMDNodeNumOperands(returnValueOperand));
			DynamicLLVM::LLVMGetMDNodeOperands(returnValueOperand, returnValueMemberInfo.data());

			bool isSpecial = false;

			for (auto& memberInfo: returnValueMemberInfo) {
				auto str = llvmMDStringToStringView(memberInfo);

				if (str == "air.position") {
					// this is the special position return value
					_positionOutputIndex = i;
					isSpecial = true;
					_outputValueIDs.push_back(SPIRV::ResultIDInvalid);
					break;
				}
			}

			if (!isSpecial) {
				auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMStructGetTypeAtIndex(funcRetType, i));
				auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type, 8));
				auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Output);
				builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { location } });
				++location;
				_outputValueIDs.push_back(var);
				builder.referenceGlobalVariable(var);
			}
		}
	} else if (DynamicLLVM::LLVMGetTypeKind(funcRetType) != LLVMVoidTypeKind) {
		// TODO: handle case of returning a special variable (like air.position) with a single return value

		auto type = llvmTypeToSPIRVType(builder, funcRetType);
		auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type, 8));
		auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Output);
		builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { 0 } });
		_outputValueIDs.push_back(var);
		builder.referenceGlobalVariable(var);
	}

	std::vector<LLVMValueRef> parameterOperands(DynamicLLVM::LLVMGetMDNodeNumOperands(rootInfoOperands[2]));
	DynamicLLVM::LLVMGetMDNodeOperands(rootInfoOperands[2], parameterOperands.data());

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
		std::vector<LLVMValueRef> parameterInfo(DynamicLLVM::LLVMGetMDNodeNumOperands(parameterOperand));
		DynamicLLVM::LLVMGetMDNodeOperands(parameterOperand, parameterInfo.data());

		auto kind = llvmMDStringToStringView(parameterInfo[1]);

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
			std::vector<LLVMValueRef> parameterInfo(DynamicLLVM::LLVMGetMDNodeNumOperands(parameterOperand));
			DynamicLLVM::LLVMGetMDNodeOperands(parameterOperand, parameterInfo.data());

			auto kind = llvmMDStringToStringView(parameterInfo[1]);

			if (kind == "air.buffer") {
				auto type = llvmTypeToSPIRVType(builder, DynamicLLVM::LLVMGetElementType(funcParamTypes[i]));
				auto addrPtrTypeInst = SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::PhysicalStorageBuffer, type, 8);
				auto addrPtrType = builder.declareType(addrPtrTypeInst);
				bufferMembers.push_back(SPIRV::Type::Member { addrPtrType, 8 * bufferIndex, {} });
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
		std::vector<LLVMValueRef> parameterInfo(DynamicLLVM::LLVMGetMDNodeNumOperands(parameterOperand));
		DynamicLLVM::LLVMGetMDNodeOperands(parameterOperand, parameterInfo.data());

		auto kind = llvmMDStringToStringView(parameterInfo[1]);
		auto llparam = DynamicLLVM::LLVMGetParam(_function, i);
		auto llparamVal = reinterpret_cast<uintptr_t>(llparam);

		if (kind == "air.vertex_id") {
			_vertexIDInputIndex = i;
			auto load = builder.encodeLoad(intType, vertexIndexVar);
			_parameterIDs.push_back(load);
			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, intType);
		} else if (kind == "air.buffer") {
			// find the location index info
			size_t infoIdx = 0;
			for (; infoIdx < parameterInfo.size(); ++infoIdx) {
				if (DynamicLLVM::LLVMIsAMDString(parameterInfo[infoIdx])) {
					auto str = llvmMDStringToStringView(parameterInfo[infoIdx]);

					if (str == "air.location_index") {
						break;
					}
				}
			}

			if (infoIdx >= parameterInfo.size()) {
				// weird, location index info not found
				std::runtime_error("Failed to find location index info for buffer");
			}

			uint32_t bindingIndex = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 1]);
			auto somethingElseTODO = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 2]);

			funcInfo.bindings.push_back(BindingInfo { BindingType::Buffer, bindingIndex, 0 });

			auto ptrType = bufferMembers[bufferIndex].id;
			auto ptrPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Uniform, ptrType, 8));
			auto access = builder.encodeAccessChain(ptrPtrType, uboPointersVar, { builder.declareConstantScalar<int32_t>(bufferIndex) });
			auto load = builder.encodeLoad(ptrType, access);

			_parameterIDs.push_back(load);

			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, ptrType);

			++bufferIndex;
		} else if (kind == "air.position") {
			auto load = builder.encodeLoad(vec4Type, fragCoordVar);
			_parameterIDs.push_back(load);
			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, vec4Type);
		} else if (kind == "air.fragment_input") {
			auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
			auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, type, 8));
			auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Input);
			auto load = builder.encodeLoad(type, var);

			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { paramLocation } });
			++paramLocation;

			builder.referenceGlobalVariable(var);

			_parameterIDs.push_back(load);

			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, type);
		} else if (kind == "air.vertex_input") {
			auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
			auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, type, 8));
			auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Input);
			auto load = builder.encodeLoad(type, var);

			// find the location index info
			size_t infoIdx = 0;
			for (; infoIdx < parameterInfo.size(); ++infoIdx) {
				if (DynamicLLVM::LLVMIsAMDString(parameterInfo[infoIdx])) {
					auto str = llvmMDStringToStringView(parameterInfo[infoIdx]);

					if (str == "air.location_index") {
						break;
					}
				}
			}

			if (infoIdx >= parameterInfo.size()) {
				// weird, location index info not found
				std::runtime_error("Failed to find location index info for buffer");
			}

			uint32_t locationIndex = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 1]);
			auto somethingElseTODO = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 2]);

			funcInfo.bindings.push_back(BindingInfo { BindingType::VertexInput, locationIndex, /* ignored: */ 0, /* ignored: */ TextureAccessType::Read, /* ignored: */ 0 });

			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { locationIndex } });

			builder.referenceGlobalVariable(var);

			_parameterIDs.push_back(load);

			builder.associateExistingResultID(load, llparamVal);
			builder.setResultType(load, type);
		} else if (kind == "air.thread_position_in_grid") {
			auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
			auto load = builder.encodeLoad(uvec3Type, globalInvocationIdVar);
			auto typeInst = *builder.reverseLookupType(type);
			auto target = load;
			auto resultType = type;

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
				auto uvec2Type = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 2, u32Type, 8, 8));
				target = builder.encodeVectorShuffle(uvec2Type, load, load, { 0, 1 });
				resultType = uvec2Type;
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
		std::vector<LLVMValueRef> parameterInfo(DynamicLLVM::LLVMGetMDNodeNumOperands(parameterOperand));
		DynamicLLVM::LLVMGetMDNodeOperands(parameterOperand, parameterInfo.data());

		auto kind = llvmMDStringToStringView(parameterInfo[1]);

		if (kind == "air.texture") {
			// find the location index info, determine the access type, and find the arg type info
			size_t infoIdx = SIZE_MAX;
			TextureAccessType accessType = TextureAccessType::Sample;
			size_t argTypeIdx = SIZE_MAX;
			for (size_t idx = 0; idx < parameterInfo.size(); ++idx) {
				if (DynamicLLVM::LLVMIsAMDString(parameterInfo[idx])) {
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
				std::runtime_error("Failed to find location index info for buffer");
			}

			if (argTypeIdx >= parameterInfo.size()) {
				std::runtime_error("Failed to find arg type info for buffer");
			}

			uint32_t bindingIndex = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 1]);
			auto somethingElseTODO = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 2]);
			auto argType = llvmMDStringToStringView(parameterInfo[argTypeIdx + 1]);
			auto firstAngle = argType.find('<');
			auto lastAngle = argType.find('>');
			auto comma = argType.find(',');
			auto notWhitespace = argType.find_first_not_of(" ", comma + 1);

			std::string_view textureClassName(argType.data(), firstAngle);
			std::string_view sampleTypeName(argType.data() + (firstAngle + 1), comma - (firstAngle + 1));
			std::string_view accessTypeName(argType.data() + notWhitespace, lastAngle - notWhitespace);

			funcInfo.bindings.push_back(BindingInfo { BindingType::Texture, bindingIndex, internalBindingIndex, accessType });

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
			}

			// TODO: same here (find a better way...)
			if (textureClassName == "texture2d") {
				dimensionality = SPIRV::Dim::e2D;
			} else if (textureClassName == "texturecube") {
				dimensionality = SPIRV::Dim::eCube;
			}

			auto imageType = builder.declareType(SPIRV::Type(SPIRV::Type::ImageTag {}, fakeSampleType, realSampleType, dimensionality, 2, false, false, accessType == TextureAccessType::Sample ? 1 : 2, SPIRV::ImageFormat::Unknown));
			auto imagePtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::UniformConstant, imageType, 8));
			auto var = builder.addGlobalVariable(imagePtrType, SPIRV::StorageClass::UniformConstant);
			//auto load = builder.encodeLoad(imageType, var);

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
		std::vector<LLVMValueRef> parameterInfo(DynamicLLVM::LLVMGetMDNodeNumOperands(parameterOperand));
		DynamicLLVM::LLVMGetMDNodeOperands(parameterOperand, parameterInfo.data());

		auto kind = llvmMDStringToStringView(parameterInfo[1]);

		if (kind == "air.sampler") {
			// find the location index info
			size_t infoIdx = 0;
			for (; infoIdx < parameterInfo.size(); ++infoIdx) {
				if (DynamicLLVM::LLVMIsAMDString(parameterInfo[infoIdx])) {
					auto str = llvmMDStringToStringView(parameterInfo[infoIdx]);

					if (str == "air.location_index") {
						break;
					}
				}
			}

			if (infoIdx >= parameterInfo.size()) {
				// weird, location index info not found
				std::runtime_error("Failed to find location index info for buffer");
			}

			uint32_t bindingIndex = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 1]);
			auto somethingElseTODO = DynamicLLVM::LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 2]);

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

			auto init = DynamicLLVM::LLVMGetInitializer(suboperands[1]);
			auto val = DynamicLLVM::LLVMConstIntGetZExtValue(init);

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

			builder.associateExistingResultID(var, reinterpret_cast<uintptr_t>(suboperands[1]));
			builder.setResultType(var, samplerPtrType);

			++internalBindingIndex;
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

	// TODO: handle more complex CFG structures

	enum class CFGType {
		None = 0,
		Selection,
	};

	struct CFGInfo {
		CFGType type = CFGType::None;
		SPIRV::ResultID selectionMergeBlock = SPIRV::ResultIDInvalid;
	};

	std::unordered_map<SPIRV::ResultID, CFGInfo> cfgInfos;

	for (LLVMBasicBlockRef bb = DynamicLLVM::LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = DynamicLLVM::LLVMGetNextBasicBlock(bb)) {
		auto lastInst = DynamicLLVM::LLVMGetLastInstruction(bb);
		auto lastInstOpcode = DynamicLLVM::LLVMGetInstructionOpcode(lastInst);

		CFGInfo cfgInfo {};

		if (lastInstOpcode == LLVMBr && DynamicLLVM::LLVMIsConditional(lastInst)) {
			// check if this is a basic conditional/selection (i.e. an `if`)
			auto firstBlock = DynamicLLVM::LLVMGetSuccessor(lastInst, 0);
			auto secondBlock = DynamicLLVM::LLVMGetSuccessor(lastInst, 1);

			auto lastInstFirstBlock = DynamicLLVM::LLVMGetLastInstruction(firstBlock);
			auto lastInstSecondBlock = DynamicLLVM::LLVMGetLastInstruction(secondBlock);
			auto lastInstOpcodeFirstBlock = DynamicLLVM::LLVMGetInstructionOpcode(lastInstFirstBlock);
			auto lastInstOpcodeSecondBlock = DynamicLLVM::LLVMGetInstructionOpcode(lastInstSecondBlock);

			SPIRV::ResultID mergeBlock = SPIRV::ResultIDInvalid;

			// check if the first block is the special block
			if (lastInstOpcodeFirstBlock == LLVMBr && !DynamicLLVM::LLVMIsConditional(lastInstFirstBlock)) {
				// make sure the target block is the same
				auto block = DynamicLLVM::LLVMGetSuccessor(lastInstFirstBlock, 0);

				if (block == secondBlock) {
					// perfect, the second block is the merge block then
					mergeBlock = builder.lookupResultID(reinterpret_cast<uintptr_t>(secondBlock));
				}
			} else if (lastInstOpcodeSecondBlock == LLVMBr && !DynamicLLVM::LLVMIsConditional(lastInstSecondBlock)) {
				// make sure the target block is the same
				auto block = DynamicLLVM::LLVMGetSuccessor(lastInstSecondBlock, 0);

				if (block == firstBlock) {
					// perfect, the first block is the merge block then
					mergeBlock = builder.lookupResultID(reinterpret_cast<uintptr_t>(firstBlock));
				}
			}

			if (mergeBlock != SPIRV::ResultIDInvalid) {
				cfgInfo.type = CFGType::Selection;
				cfgInfo.selectionMergeBlock = mergeBlock;
			}
		}

		if (cfgInfo.type != CFGType::None) {
			cfgInfos[builder.lookupResultID(reinterpret_cast<uintptr_t>(bb))] = cfgInfo;
		}
	}

	//
	// translate instructions
	//
	isFirst = true;
	for (LLVMBasicBlockRef bb = DynamicLLVM::LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = DynamicLLVM::LLVMGetNextBasicBlock(bb)) {
		if (isFirst) {
			isFirst = false;

			// we already have a label inserted
		} else {
			// insert a new label
			builder.insertLabel(builder.lookupResultID(reinterpret_cast<uintptr_t>(bb)));
		}

		for (LLVMValueRef inst = DynamicLLVM::LLVMGetFirstInstruction(bb); inst != nullptr; inst = DynamicLLVM::LLVMGetNextInstruction(inst)) {
			auto opcode = DynamicLLVM::LLVMGetInstructionOpcode(inst);

			switch (opcode) {
				case LLVMZExt: {
					auto source = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto resID = builder.encodeUConvert(type, llvmValueToResultID(builder, source));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				// TODO: look into using physical addressing instead of logical addressing, which should simplify the translation process
				//       (since i *think* that's the same addressing mode used by Metal code)

				case LLVMGetElementPtr: {
					auto base = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);

					std::vector<SPIRV::ResultID> indices;
					auto operandCount = DynamicLLVM::LLVMGetNumOperands(inst);

					for (size_t i = 1; i < operandCount; ++i) {
						auto llindex = DynamicLLVM::LLVMGetOperand(inst, i);
						indices.push_back(llvmValueToResultID(builder, llindex));
					}

					auto tmp = llvmTypeToSPIRVType(builder, targetType);
					auto tmp2 = llvmValueToResultID(builder, base);

					// ensure the resulting pointer storage class is the same as the input pointer storage class
					auto type = *builder.reverseLookupType(tmp);
					auto origTypeID = builder.lookupResultType(tmp2);
					auto origType = *builder.reverseLookupType(origTypeID);
					type.pointerStorageClass = origType.pointerStorageClass;
					auto resultType = builder.declareType(type);
					auto origTypeTarget = *builder.reverseLookupType(origType.targetType);

					// strangely enough, SPIR-V provides no instruction that can do an initial pointer offset like LLVM's GEP does.
					// there's OpPtrAccessChain, which is tantalizingly named, but unfortunately that instruction doesn't work as expected
					// either. i've tried using the raw index (i.e. in element units) and the multiplied index (i.e. in bytes), but no dice.
					// so, let's do it ourselves. a little pointer arithmetic never hurt anybody, right? (it most certainly has).
					auto uint64Type = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 64, false));
					auto asInteger = builder.encodeConvertPtrToU(uint64Type, tmp2);
					auto mul = builder.encodeArithBinop(SPIRV::Opcode::IMul, uint64Type, indices[0], builder.declareConstantScalar<uint64_t>(origTypeTarget.size));
					auto added = builder.encodeArithBinop(SPIRV::Opcode::IAdd, uint64Type, asInteger, mul);
					auto asPtr = builder.encodeConvertUToPtr(origTypeID, added);

					indices.erase(indices.begin());

					SPIRV::ResultID resID = SPIRV::ResultIDInvalid;

					if (indices.size() > 0) {
						resID = builder.encodeAccessChain(resultType, asPtr, indices);
					} else {
						resID = asPtr;
					}

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, resultType);
				} break;

				case LLVMLoad: {
					auto alignment = DynamicLLVM::LLVMGetAlignment(inst);
					auto ptr = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto targetType = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);
					auto op = llvmValueToResultID(builder, ptr);
					auto opType = *builder.reverseLookupType(builder.lookupResultType(op));
					auto opDerefType = *builder.reverseLookupType(opType.targetType);

					if (opDerefType.backingType == SPIRV::Type::BackingType::RuntimeArray) {
						// requires an additional index
						auto accessType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, opType.pointerStorageClass, type, 8));
						auto access = builder.encodeAccessChain(accessType, op, { builder.declareConstantScalar<int32_t>(0) });
						builder.setResultType(access, accessType);
						op = access;
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

					// TODO: also handle runtime arrays properly here

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
					auto type = llvmTypeToSPIRVType(builder, targetType);

					if (name == "air.convert.f.v2f32.u.v2i32") {
						auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);

						resID = builder.encodeConvertUToF(type, llvmValueToResultID(builder, arg));
					} else if (name == "air.sample_texture_2d.v4f16" || name == "air.sample_texture_2d.v4f32" || name == "air.sample_texture_cube.v4f16") {
						auto textureArg = DynamicLLVM::LLVMGetOperand(inst, 0);
						auto samplerArg = DynamicLLVM::LLVMGetOperand(inst, 1);
						auto textureCoordArg = DynamicLLVM::LLVMGetOperand(inst, 2);
						auto someBooleanArgTODO = DynamicLLVM::LLVMGetOperand(inst, 3);
						auto offsetArg = DynamicLLVM::LLVMGetOperand(inst, 4);
						auto someOtherBooleanArgTODO = DynamicLLVM::LLVMGetOperand(inst, 5);
						auto someFloatArgTODO = DynamicLLVM::LLVMGetOperand(inst, 6);
						auto someOtherFloatArgTODO = DynamicLLVM::LLVMGetOperand(inst, 6);
						auto someI32ArgTODO = DynamicLLVM::LLVMGetOperand(inst, 7);

						auto textureArgID = llvmValueToResultID(builder, textureArg);
						auto samplerArgID = llvmValueToResultID(builder, samplerArg);

						auto texturePtrType = builder.lookupResultType(textureArgID);
						auto textureType = builder.reverseLookupType(texturePtrType)->targetType;
						auto textureLoad = builder.encodeLoad(textureType, textureArgID);

						auto samplerType = builder.declareType(SPIRV::Type(SPIRV::Type::SamplerTag {}));
						auto samplerPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::UniformConstant, samplerType, 8));
#if 1
						auto samplerLoad = builder.encodeLoad(samplerType, samplerArgID);
#else
						auto samplerArgCast = builder.encodeBitcast(samplerPtrType, samplerArgID);
						auto samplerLoad = builder.encodeLoad(samplerType, samplerArgCast);
#endif

						auto sampledImageType = builder.declareType(SPIRV::Type(SPIRV::Type::SampledImageTag {}, textureType));
						auto sampledImage = builder.encodeSampledImage(sampledImageType, textureLoad, samplerLoad);

						// WORKAROUND: as explained before, we use 32-bit floats in place of 16-bit floats for image sampling.
						auto textureSampleComponentType = builder.declareType(SPIRV::Type(SPIRV::Type::FloatTag {}, 32));
						auto textureSampleType = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 4, textureSampleComponentType, 16, 8));
						auto sampled = builder.encodeImageSampleImplicitLod(textureSampleType, sampledImage, llvmValueToResultID(builder, textureCoordArg));

						if (name == "air.sample_texture_2d.v4f16" || name == "air.sample_texture_cube.v4f16") {
							auto convertedComponentType = builder.declareType(SPIRV::Type(SPIRV::Type::FloatTag {}, 16));
							auto convertedType = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 4, convertedComponentType, 8, 8));
							sampled = builder.encodeFConvert(convertedType, sampled);
						}

						auto partialResult = builder.encodeCompositeInsert(type, sampled, builder.declareUndefinedValue(type), { 0 });
						resID = builder.encodeCompositeInsert(type, builder.declareConstantScalar<int8_t>(0), partialResult, { 1 });
					} else if (name == "air.convert.f.v4f32.f.v4f16" || name == "air.convert.f.v4f16.f.v4f32") {
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
					} else {
						throw std::runtime_error(std::string("TODO: support actual function calls (name = ") + name.data() + ")");
					}

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMFMul:
				case LLVMFDiv:
				case LLVMFAdd:
				case LLVMFSub: {
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

					SPIRV::ResultID resID = SPIRV::ResultIDInvalid;
					auto type = builder.declareType(SPIRV::Type(SPIRV::Type::BooleanTag {}));

					switch (predicate) {
						case LLVMRealOGT: {
							resID = builder.encodeFOrdGreaterThan(llvmValueToResultID(builder, op1), llvmValueToResultID(builder, op2));
						} break;

						case LLVMRealOLT: {
							resID = builder.encodeFOrdLessThan(llvmValueToResultID(builder, op1), llvmValueToResultID(builder, op2));
						} break;

						default:
							throw std::runtime_error("TODO: handle fcmp predicate: " + std::to_string(predicate));
					}

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMBr: {
					if (DynamicLLVM::LLVMIsConditional(inst)) {
						auto condition = DynamicLLVM::LLVMGetCondition(inst);
						auto trueLabel = DynamicLLVM::LLVMBasicBlockAsValue(DynamicLLVM::LLVMGetSuccessor(inst, 0));
						auto falseLabel = DynamicLLVM::LLVMBasicBlockAsValue(DynamicLLVM::LLVMGetSuccessor(inst, 1));

						// TODO: detect more CFG structures

						auto it = cfgInfos.find(builder.lookupResultID(reinterpret_cast<uintptr_t>(bb)));

						if (it != cfgInfos.end()) {
							auto& cfgInfo = it->second;

							switch (cfgInfo.type) {
								case CFGType::Selection: {
									builder.encodeSelectionMerge(cfgInfo.selectionMergeBlock);
								} break;

								default:
									break;
							}
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

					auto opCount = DynamicLLVM::LLVMCountIncoming(inst);
					for (size_t i = 0; i < opCount; ++i) {
						auto variable = DynamicLLVM::LLVMGetIncomingValue(inst, i);
						auto label = DynamicLLVM::LLVMBasicBlockAsValue(DynamicLLVM::LLVMGetIncomingBlock(inst, i));
						variablesAndBlocks.push_back(std::make_pair(llvmValueToResultID(builder, variable), llvmValueToResultID(builder, label)));
					}

					auto resID = builder.encodePhi(type, variablesAndBlocks);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMBitCast: {
					auto arg = DynamicLLVM::LLVMGetOperand(inst, 0);
					auto lltype = DynamicLLVM::LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, lltype);
					auto argID = llvmValueToResultID(builder, arg);

					// ensure the resulting pointer storage class is the same as the input pointer storage class
					auto typeInst = *builder.reverseLookupType(type);
					auto origType = builder.lookupResultType(argID);
					auto origTypeInst = *builder.reverseLookupType(origType);
					typeInst.pointerStorageClass = origTypeInst.pointerStorageClass;
					auto resultType = builder.declareType(typeInst);

					auto resID = builder.encodeBitcast(resultType, argID);

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

				default:
					throw std::runtime_error("Unhandled instruction opcode: " + std::to_string(opcode));
			}
		}
	}

	builder.endFunction();
};

Iridium::AIR::Library::Library(const void* data, size_t size) {
	ByteReader reader(data, size);

	// the header has a minimum length of 88 bytes
	if (size < 88) {
		// TODO
		abort();
	}

	if (reader.readString(4) != "MTLB") {
		// invalid file ID

		// TODO
		abort();
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
				auto funcBCOffset = subreader.readIntegerLE<uint64_t>();

				bitcode = static_cast<const char*>(data) + bcOffset + funcBCOffset;
			} else {
				subreader.skip(tagSize);
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
