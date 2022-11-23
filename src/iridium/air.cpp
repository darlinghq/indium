#include <iridium/air.hpp>
#include <iridium/bits.hpp>
#include <iridium/spirv.hpp>

#include <llvm-c/BitReader.h>

// special thanks to https://github.com/YuAo/MetalLibraryArchive for information on the library format

// we use LLVM's C API because it's much more stable than the C++ API and it's good enough for our needs

Iridium::AIR::Function::Function(Type type, const std::string& name, const void* bitcode, size_t bitcodeSize):
	_name(name),
	_type(type)
{
	LLVMModuleRef moduleRef;
	_bitcodeBuffer = LLVMSupport::MemoryBuffer(LLVMCreateMemoryBufferWithMemoryRange(reinterpret_cast<const char*>(bitcode), bitcodeSize, "", false), LLVMDisposeMemoryBuffer);

	if (!_bitcodeBuffer) {
		// TODO
		abort();
	}

	if (LLVMParseBitcode2(_bitcodeBuffer.get(), &moduleRef)) {
		// TODO
		abort();
	}

	_module = LLVMSupport::Module(moduleRef, LLVMDisposeModule);

	_function = LLVMGetNamedFunction(_module.get(), _name.c_str());

	if (!_function) {
		// TODO
		abort();
	}
};

// TEST
#include <iostream>

static Iridium::SPIRV::ResultID llvmTypeToSPIRVType(Iridium::SPIRV::Builder& builder, LLVMTypeRef llvmType) {
	using namespace Iridium::SPIRV;

	auto kind = LLVMGetTypeKind(llvmType);

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
			return builder.declareType(Type(Type::IntegerTag {}, LLVMGetIntTypeWidth(llvmType), /* TODO: how to determine this? */ true));

		case LLVMFunctionTypeKind: {
			auto retType = llvmTypeToSPIRVType(builder, LLVMGetReturnType(llvmType));
			std::vector<ResultID> paramTypes;
			std::vector<LLVMTypeRef> llvmParamTypes(LLVMCountParamTypes(llvmType));
			LLVMGetParamTypes(llvmType, llvmParamTypes.data());
			for (auto& typeRef: llvmParamTypes) {
				paramTypes.push_back(llvmTypeToSPIRVType(builder, typeRef));
			}
			return builder.declareType(Type(Type::FunctionTag {}, retType, paramTypes, 8));
		} break;

		case LLVMStructTypeKind: {
			std::vector<Type::Member> members;
			auto len = LLVMCountStructElementTypes(llvmType);
			// unfortunately, LLVM's C API provides no way to determine the offset of a structure member.
			// nor does it provide a way to determine the size or alignment of a type as a constant expression.
			// therefore, we'll need to calculate these values ourselves.
			auto structAlignment = 1;
			if (!LLVMIsPackedStruct(llvmType)) {
				// see which member has the greatest alignment
				for (size_t i = 0; i < len; ++i) {
					auto typeAtIndex = LLVMStructGetTypeAtIndex(llvmType, i);
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
				auto typeAtIndex = LLVMStructGetTypeAtIndex(llvmType, i);
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
			//auto addrSpace = LLVMGetPointerAddressSpace(llvmType);
			return builder.declareType(Type(Type::PointerTag {}, storageClass, llvmTypeToSPIRVType(builder, LLVMGetElementType(llvmType)), 8));
		} break;

		case LLVMVectorTypeKind: {
			auto type = llvmTypeToSPIRVType(builder, LLVMGetElementType(llvmType));
			auto typeInst = *builder.reverseLookupType(type);
			auto elmCount = LLVMGetVectorSize(llvmType);
			return builder.declareType(Type(Type::VectorTag {}, elmCount, type, typeInst.size * elmCount, ((elmCount == 3 || elmCount == 4) ? 4 : 2) * typeInst.alignment));
		} break;

		default:
			return ResultIDInvalid;
	}
};

static std::string_view llvmMDStringToStringView(LLVMValueRef llvmMDString) {
	unsigned int length = 0;
	auto rawStr = LLVMGetMDString(llvmMDString, &length);
	return std::string_view(rawStr, length);
};

static Iridium::SPIRV::ResultID llvmValueToResultID(Iridium::SPIRV::Builder& builder, LLVMValueRef llvmValue) {
	using namespace Iridium::SPIRV;

	auto kind = LLVMGetValueKind(llvmValue);
	auto type = LLVMTypeOf(llvmValue);
	auto typeKind = LLVMGetTypeKind(type);

	switch (kind) {
		case LLVMConstantIntValueKind: {
			auto val = LLVMConstIntGetZExtValue(llvmValue);
			auto width = LLVMGetIntTypeWidth(type );
			// TODO: how to determine if it's signed or not?
			return (width <= 32) ? builder.declareConstantScalar<int32_t>(val) : builder.declareConstantScalar<int64_t>(val);
		} break;

		case LLVMConstantFPValueKind: {
			LLVMBool losesInfo = false;
			auto val = LLVMConstRealGetDouble(llvmValue, &losesInfo);

			switch (typeKind) {
				case LLVMFloatTypeKind:
					return builder.declareConstantScalar<float>(val);

				case LLVMDoubleTypeKind:
					return builder.declareConstantScalar<double>(val);

				default:
					return ResultIDInvalid;
			}
		} break;

		case LLVMConstantVectorValueKind: {
			std::vector<ResultID> vals;
			auto count = LLVMGetVectorSize(type);

			for (size_t i = 0; i < count; ++i) {
				auto constant = LLVMGetOperand(llvmValue, i);
				vals.push_back(llvmValueToResultID(builder, constant));
			}

			return builder.declareConstantComposite(llvmTypeToSPIRVType(builder, type), vals);
		} break;

		case LLVMConstantDataVectorValueKind: {
			std::vector<ResultID> vals;

			auto count = LLVMGetVectorSize(type);

			for (size_t i = 0; i < count; ++i) {
				auto constant = LLVMGetElementAsConstant(llvmValue, i);
				vals.push_back(llvmValueToResultID(builder, constant));
			}

			return builder.declareConstantComposite(llvmTypeToSPIRVType(builder, type), vals);
		} break;

		case LLVMUndefValueValueKind: {
			return builder.declareUndefinedValue(llvmTypeToSPIRVType(builder, type));
		} break;

		default:
			return builder.lookupResultID(reinterpret_cast<uintptr_t>(llvmValue));
	}
};

void Iridium::AIR::Function::analyze(SPIRV::Builder& builder, OutputInfo& outputInfo) {
	//auto tmp = LLVMPrintModuleToString(_module.get());
	//std::cout << "    " << tmp << std::endl;
	//LLVMDisposeMessage(tmp);

	auto& funcInfo = outputInfo.functionInfos[_name];

	auto namedMD = LLVMGetFirstNamedMetadata(_module.get());

	auto voidType = builder.declareType(SPIRV::Type(SPIRV::Type::VoidTag {}));
	auto funcType = builder.declareType(SPIRV::Type(SPIRV::Type::FunctionTag {}, voidType, {}, 8));
	auto funcID = builder.declareFunction(funcType);
	auto intType = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 32, true));
	auto ptrIntType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, intType, 8));
	auto floatTypeInst = SPIRV::Type(SPIRV::Type::FloatTag {}, 32);
	auto floatType = builder.declareType(floatTypeInst);
	auto vec4Type = builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 4, floatType, floatTypeInst.size * 4, 16));

	SPIRV::ResultID perVertexVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID vertexIndexVar = SPIRV::ResultIDInvalid;
	SPIRV::ResultID fragCoordVar = SPIRV::ResultIDInvalid;

	builder.beginFunction(funcID.id);

	// within this node, operand 0 refers to the vertex function,
	// operand 1 contains information about the function's return value,
	// and operand 2 contains information about the function's parameters.
	std::vector<LLVMValueRef> rootInfoOperands;

	if (auto vertexMD = LLVMGetNamedMetadata(_module.get(), "air.vertex", sizeof("air.vertex") - 1)) {
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
		std::vector<LLVMValueRef> operands(LLVMGetNamedMetadataNumOperands(_module.get(), "air.vertex"));
		LLVMGetNamedMetadataOperands(_module.get(), "air.vertex", operands.data());

		// operand 0 contains the info for the vertex shader
		auto rootInfoMD = operands[0];

		rootInfoOperands = std::vector<LLVMValueRef>(LLVMGetMDNodeNumOperands(rootInfoMD));
		LLVMGetMDNodeOperands(rootInfoMD, rootInfoOperands.data());
	} else if (auto fragmentMD = LLVMGetNamedMetadata(_module.get(), "air.fragment", sizeof("air.fragment") - 1)) {
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
		std::vector<LLVMValueRef> operands(LLVMGetNamedMetadataNumOperands(_module.get(), "air.fragment"));
		LLVMGetNamedMetadataOperands(_module.get(), "air.fragment", operands.data());

		// operand 0 contains the info for the fragment shader
		auto rootInfoMD = operands[0];

		rootInfoOperands = std::vector<LLVMValueRef>(LLVMGetMDNodeNumOperands(rootInfoMD));
		LLVMGetMDNodeOperands(rootInfoMD, rootInfoOperands.data());
	}

	// TODO: inspect the output of some more compiled shaders; the current code is only valid for
	//       shaders that return simple structures, vectors, or scalars as outputs.

	// SPIR-V doesn't allow structures to have a storage class of "output", so
	// if the function returns a structure, we need to separate the components
	// into separate output variables.

	auto llfuncType = LLVMGetElementType(LLVMTypeOf(_function));
	auto funcRetType = LLVMGetReturnType(llfuncType);
	std::vector<LLVMTypeRef> funcParamTypes(LLVMCountParamTypes(llfuncType));
	LLVMGetParamTypes(llfuncType, funcParamTypes.data());

	std::vector<LLVMValueRef> returnValueOperands(LLVMGetMDNodeNumOperands(rootInfoOperands[1]));
	LLVMGetMDNodeOperands(rootInfoOperands[1], returnValueOperands.data());

	if (LLVMGetTypeKind(funcRetType) == LLVMStructTypeKind) {
		// analyze return value and mark special values (like the position)
		uint32_t location = 0;
		for (size_t i = 0; i < returnValueOperands.size(); ++i) {
			auto& returnValueOperand = returnValueOperands[i];

			std::vector<LLVMValueRef> returnValueMemberInfo(LLVMGetMDNodeNumOperands(returnValueOperand));
			LLVMGetMDNodeOperands(returnValueOperand, returnValueMemberInfo.data());

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
				auto type = llvmTypeToSPIRVType(builder, LLVMStructGetTypeAtIndex(funcRetType, i));
				auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type, 8));
				auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Output);
				builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { location } });
				++location;
				_outputValueIDs.push_back(var);
				builder.referenceGlobalVariable(var);
			}
		}
	} else {
		// TODO: handle case of returning a special variable (like air.position) with a single return value

		auto type = llvmTypeToSPIRVType(builder, funcRetType);
		auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type, 8));
		auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Output);
		builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Location, { 0 } });
		_outputValueIDs.push_back(var);
		builder.referenceGlobalVariable(var);
	}

	std::vector<LLVMValueRef> parameterOperands(LLVMGetMDNodeNumOperands(rootInfoOperands[2]));
	LLVMGetMDNodeOperands(rootInfoOperands[2], parameterOperands.data());

	// analyze parameters and mark special values (like the vertex ID)
	uint32_t paramLocation = 0;
	for (size_t i = 0; i < parameterOperands.size(); ++i) {
		auto& parameterOperand = parameterOperands[i];

		// info[0] is the parameter index
		// info[1] is the kind
		// everything after that depends on the kind
		std::vector<LLVMValueRef> parameterInfo(LLVMGetMDNodeNumOperands(parameterOperand));
		LLVMGetMDNodeOperands(parameterOperand, parameterInfo.data());

		auto kind = llvmMDStringToStringView(parameterInfo[1]);

		if (kind == "air.vertex_id") {
			_vertexIDInputIndex = i;
			auto load = builder.encodeLoad(intType, vertexIndexVar);
			_parameterIDs.push_back(load);
			builder.associateExistingResultID(load, reinterpret_cast<uintptr_t>(LLVMGetParam(_function, i)));
			builder.setResultType(load, intType);
		} else if (kind == "air.buffer") {
			// find the location index info
			size_t infoIdx = 0;
			for (; infoIdx < parameterInfo.size(); ++infoIdx) {
				if (LLVMIsAMDString(parameterInfo[infoIdx])) {
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

			uint32_t bindingIndex = LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 1]);
			auto somethingElseTODO = LLVMConstIntGetSExtValue(parameterInfo[infoIdx + 2]);

			if (bindingIndex >= funcInfo.bindings.size()) {
				funcInfo.bindings.resize(bindingIndex + 1);
			}

			funcInfo.bindings[bindingIndex] = BindingInfo { BindingType::Buffer };

			auto type = llvmTypeToSPIRVType(builder, LLVMGetElementType(funcParamTypes[i]));
			auto typeInst = *builder.reverseLookupType(type);
			auto arrTypeInst = SPIRV::Type(SPIRV::Type::RuntimeArrayTag {}, type, typeInst.size, typeInst.alignment);
			auto arrType = builder.declareType(arrTypeInst);
			auto blockType = builder.declareType(SPIRV::Type(SPIRV::Type::StructureTag {}, { SPIRV::Type::Member { arrType, 0, {} } }, 0, arrTypeInst.alignment));

			builder.addDecoration(blockType, SPIRV::Decoration { SPIRV::DecorationType::Block, {} });

			auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::StorageBuffer, blockType, 8));
			auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::StorageBuffer);
			auto accessType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::StorageBuffer, arrType, 8));
			auto varArrPtr = builder.encodeAccessChain(accessType, var, { builder.declareConstantScalar<int32_t>(0) });

			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::DescriptorSet, { 0 } });
			builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Binding, { bindingIndex } });

			builder.referenceGlobalVariable(var);

			_parameterIDs.push_back(varArrPtr);

			builder.associateExistingResultID(varArrPtr, reinterpret_cast<uintptr_t>(LLVMGetParam(_function, i)));
			builder.setResultType(varArrPtr, accessType);
		} else if (kind == "air.position") {
			auto load = builder.encodeLoad(vec4Type, fragCoordVar);
			_parameterIDs.push_back(load);
			builder.associateExistingResultID(load, reinterpret_cast<uintptr_t>(LLVMGetParam(_function, i)));
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

			builder.associateExistingResultID(load, reinterpret_cast<uintptr_t>(LLVMGetParam(_function, i)));
			builder.setResultType(load, type);
		}
	}

	for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = LLVMGetNextBasicBlock(bb)) {
		for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst != nullptr; inst = LLVMGetNextInstruction(inst)) {
			auto opcode = LLVMGetInstructionOpcode(inst);

			switch (opcode) {
				case LLVMZExt: {
					auto source = LLVMGetOperand(inst, 0);
					auto targetType = LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto resID = builder.encodeUConvert(type, llvmValueToResultID(builder, source));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				// TODO: look into using physical addressing instead of logical addressing, which should simplify the translation process
				//       (since i *think* that's the same addressing mode used by Metal code)

				case LLVMGetElementPtr: {
					// TODO: revisit this!
					//       this will not work in some cases e.g. when we have a pointer to local variable.
					//       maybe we should mark certain IDs as requiring omission of the first GEP operand
					//       (since the first operand would be used to index into the pointer, but this is not necessary (nor allowed)
					//       with SPIR-V).
					auto base = LLVMGetOperand(inst, 0);
					auto targetType = LLVMTypeOf(inst);

					std::vector<SPIRV::ResultID> indices;
					auto operandCount = LLVMGetNumOperands(inst);

					for (size_t i = 1; i < operandCount; ++i) {
						auto llindex = LLVMGetOperand(inst, i);
						indices.push_back(llvmValueToResultID(builder, llindex));
					}

					auto tmp = llvmTypeToSPIRVType(builder, targetType);
					auto tmp2 = llvmValueToResultID(builder, base);

					// ensure the resulting pointer storage class is the same as the input pointer storage class
					auto type = *builder.reverseLookupType(tmp);
					auto origType = *builder.reverseLookupType(builder.lookupResultType(tmp2));
					type.pointerStorageClass = origType.pointerStorageClass;
					auto resultType = builder.declareType(type);

					auto resID = builder.encodeAccessChain(resultType, tmp2, indices);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, resultType);
				} break;

				case LLVMLoad: {
					auto alignment = LLVMGetAlignment(inst);
					auto ptr = LLVMGetOperand(inst, 0);
					auto targetType = LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);
					auto op = llvmValueToResultID(builder, ptr);
					auto opType = *builder.reverseLookupType(builder.lookupResultType(op));
					auto opDerefType = *builder.reverseLookupType(opType.pointerVectorMatrixArrayTargetType);

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

				case LLVMCall: {
					auto target = LLVMGetCalledValue(inst);
					auto targetType = LLVMTypeOf(inst);

					size_t len = 0;
					auto rawName = LLVMGetValueName2(target, &len);
					std::string_view name;

					if (rawName) {
						name = std::string_view(rawName, len);
					}

					SPIRV::ResultID resID = SPIRV::ResultIDInvalid;
					auto type = llvmTypeToSPIRVType(builder, targetType);

					if (name == "air.convert.f.v2f32.u.v2i32") {
						auto arg = LLVMGetOperand(inst, 0);

						resID = builder.encodeConvertUToF(type, llvmValueToResultID(builder, arg));
					} else {
						throw std::runtime_error("TODO: support actual function calls");
					}

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMFMul: {
					auto op1 = LLVMGetOperand(inst, 0);
					auto op2 = LLVMGetOperand(inst, 1);
					auto targetType = LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto resID = builder.encodeFMul(type, llvmValueToResultID(builder, op1), llvmValueToResultID(builder, op2));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMFDiv: {
					auto op1 = LLVMGetOperand(inst, 0);
					auto op2 = LLVMGetOperand(inst, 1);
					auto targetType = LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto resID = builder.encodeFDiv(type, llvmValueToResultID(builder, op1), llvmValueToResultID(builder, op2));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMShuffleVector: {
					auto vec1 = LLVMGetOperand(inst, 0);
					auto vec2 = LLVMGetOperand(inst, 1);
					auto targetType = LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					std::vector<uint32_t> components(LLVMGetNumMaskElements(inst));

					for (size_t i = 0; i < components.size(); ++i) {
						auto val = LLVMGetMaskValue(inst, i);
						components[i] = (val == LLVMGetUndefMaskElem()) ? UINT32_MAX : val;
					}

					auto resID = builder.encodeVectorShuffle(type, llvmValueToResultID(builder, vec1), llvmValueToResultID(builder, vec2), components);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMInsertValue: {
					auto composite = LLVMGetOperand(inst, 0);
					auto part = LLVMGetOperand(inst, 1);
					auto targetType = LLVMTypeOf(inst);
					auto type = llvmTypeToSPIRVType(builder, targetType);

					auto llindices = LLVMGetIndices(inst);
					std::vector<uint32_t> indices(llindices, llindices + LLVMGetNumIndices(inst));

					auto resID = builder.encodeCompositeInsert(type, llvmValueToResultID(builder, part), llvmValueToResultID(builder, composite), indices);

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
					builder.setResultType(resID, type);
				} break;

				case LLVMRet: {
					auto llval = LLVMGetOperand(inst, 0);

					// should be the same as the function return value
					auto type = LLVMTypeOf(llval);
					auto val = llvmValueToResultID(builder, llval);

					if (LLVMGetTypeKind(type) == LLVMStructTypeKind) {
						auto structMemberCount = LLVMGetNumContainedTypes(type);

						for (size_t i = 0; i < structMemberCount; ++i) {
							auto memberType = LLVMStructGetTypeAtIndex(type, i);
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

					builder.encodeReturn();
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
