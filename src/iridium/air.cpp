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
			return builder.declareType(Type(Type::FunctionTag {}, retType, paramTypes));
		} break;

		case LLVMStructTypeKind: {
			std::vector<Type::Member> members;
			auto len = LLVMCountStructElementTypes(llvmType);
			for (size_t i = 0; i < len; ++i) {
				members.push_back(Type::Member { llvmTypeToSPIRVType(builder, LLVMStructGetTypeAtIndex(llvmType, i)), {} });
			}
			return builder.declareType(Type(Type::StructureTag {}, members));
		} break;

		case LLVMPointerTypeKind: {
			StorageClass storageClass = StorageClass::Output;
			// TODO: somehow determine the appropriate storage class
			//auto addrSpace = LLVMGetPointerAddressSpace(llvmType);
			return builder.declareType(Type(Type::PointerTag {}, storageClass, llvmTypeToSPIRVType(builder, LLVMGetElementType(llvmType))));
		} break;

		case LLVMVectorTypeKind:
			return builder.declareType(Type(Type::VectorTag {}, LLVMGetVectorSize(llvmType), llvmTypeToSPIRVType(builder, LLVMGetElementType(llvmType))));

		default:
			return ResultIDInvalid;
	}
};

static std::string_view llvmMDStringToStringView(LLVMValueRef llvmMDString) {
	unsigned int length = 0;
	auto rawStr = LLVMGetMDString(llvmMDString, &length);
	return std::string_view(rawStr, length);
};

void Iridium::AIR::Function::analyze(SPIRV::Builder& builder) {
	//auto tmp = LLVMPrintModuleToString(_module.get());
	//std::cout << "    " << tmp << std::endl;
	//LLVMDisposeMessage(tmp);

	auto namedMD = LLVMGetFirstNamedMetadata(_module.get());

	auto voidType = builder.declareType(SPIRV::Type(SPIRV::Type::VoidTag {}));
	auto funcType = builder.declareType(SPIRV::Type(SPIRV::Type::FunctionTag {}, voidType, {}));
	auto funcID = builder.declareFunction(funcType);

	builder.beginFunction(funcID.id);

	if (auto vertexMD = LLVMGetNamedMetadata(_module.get(), "air.vertex", sizeof("air.vertex") - 1)) {
		// this is a vertex shader

		builder.addEntryPoint(SPIRV::EntryPoint {
			SPIRV::ExecutionModel::Vertex,
			funcID.id,
			_name,
			{},
		});

		// declare the GLSL PerVertex structure
		auto floatType = builder.declareType(SPIRV::Type(SPIRV::Type::FloatTag {}, 32));
		auto perVertexStructType = builder.declareType(SPIRV::Type(SPIRV::Type::StructureTag {}, {
			SPIRV::Type::Member {
				builder.declareType(SPIRV::Type(SPIRV::Type::VectorTag {}, 4, floatType)),
				{
					SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::Position) } },
				},
			},
			// TODO: declare the rest of the structure
		}));
		auto perVertexStructPtrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, perVertexStructType));
		auto perVertexVar = builder.addGlobalVariable(perVertexStructPtrType, SPIRV::StorageClass::Output);

		builder.addDecoration(perVertexStructType, SPIRV::Decoration { SPIRV::DecorationType::Block });

		builder.referenceGlobalVariable(perVertexVar);

		// declare the GLSL VertexIndex input variable
		auto intType = builder.declareType(SPIRV::Type(SPIRV::Type::IntegerTag {}, 32, true));
		auto ptrIntType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Input, intType));
		auto vertexIndexVar = builder.addGlobalVariable(ptrIntType, SPIRV::StorageClass::Input);

		builder.addDecoration(vertexIndexVar, SPIRV::Decoration { SPIRV::DecorationType::Builtin, { static_cast<uint32_t>(SPIRV::BuiltinID::VertexIndex) } });

		builder.referenceGlobalVariable(vertexIndexVar);

		// get the operands
		std::vector<LLVMValueRef> operands(LLVMGetNamedMetadataNumOperands(_module.get(), "air.vertex"));
		LLVMGetNamedMetadataOperands(_module.get(), "air.vertex", operands.data());

		// operand 0 contains the info for the vertex shader
		auto rootInfoMD = operands[0];

		// within this node, operand 0 refers to the vertex function,
		// operand 1 contains information about the function's return value,
		// and operand 2 contains information about the function's parameters.
		std::vector<LLVMValueRef> rootInfoOperands(LLVMGetMDNodeNumOperands(rootInfoMD));
		LLVMGetMDNodeOperands(rootInfoMD, rootInfoOperands.data());

		// TODO: inspect the output of some more compiled shaders; the current code is only valid for
		//       shaders that return structures as outputs (and not nested structures either).

		// SPIR-V doesn't allow structures to have a storage class of "output", so
		// if the function returns a structure, we need to separate the components
		// into separate output variables.

		auto llfuncType = LLVMGetElementType(LLVMTypeOf(_function));
		auto funcRetType = LLVMGetReturnType(llfuncType);
		std::vector<LLVMTypeRef> funcParamTypes(LLVMCountParamTypes(llfuncType));
		LLVMGetParamTypes(llfuncType, funcParamTypes.data());

		if (LLVMGetTypeKind(funcRetType) != LLVMStructTypeKind) {
			throw std::runtime_error("TODO: handle functions that don't return structures");
		}

		std::vector<LLVMValueRef> returnValueOperands(LLVMGetMDNodeNumOperands(rootInfoOperands[1]));
		LLVMGetMDNodeOperands(rootInfoOperands[1], returnValueOperands.data());

		// analyze return value and mark special values (like the position)
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
				auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::Output, type));
				_outputValueIDs.push_back(builder.addGlobalVariable(ptrType, SPIRV::StorageClass::Output));
			}
		}

		std::vector<LLVMValueRef> parameterOperands(LLVMGetMDNodeNumOperands(rootInfoOperands[2]));
		LLVMGetMDNodeOperands(rootInfoOperands[2], parameterOperands.data());

		// analyze parameters and mark special values (like the vertex ID)
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
				_parameterIDs.push_back(vertexIndexVar);
				builder.associateExistingResultID(vertexIndexVar, reinterpret_cast<uintptr_t>(LLVMGetParam(_function, i)));
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

				auto type = llvmTypeToSPIRVType(builder, funcParamTypes[i]);
				auto arrType = builder.declareType(SPIRV::Type(SPIRV::Type::RuntimeArrayTag {}, type));
				auto blockType = builder.declareType(SPIRV::Type(SPIRV::Type::StructureTag {}, { SPIRV::Type::Member { arrType, {} } }));

				builder.addDecoration(blockType, SPIRV::Decoration { SPIRV::DecorationType::Block, {} });

				auto ptrType = builder.declareType(SPIRV::Type(SPIRV::Type::PointerTag {}, SPIRV::StorageClass::StorageBuffer, blockType));
				auto var = builder.addGlobalVariable(ptrType, SPIRV::StorageClass::StorageBuffer);

				builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::DescriptorSet, { 0 } });
				builder.addDecoration(var, SPIRV::Decoration { SPIRV::DecorationType::Binding, { bindingIndex } });

				builder.referenceGlobalVariable(var);

				_parameterIDs.push_back(var);

				builder.associateExistingResultID(var, reinterpret_cast<uintptr_t>(LLVMGetParam(_function, i)));
			}
		}
	} else if (auto fragmentMD = LLVMGetNamedMetadata(_module.get(), "air.fragment", sizeof("air.fragment") - 1)) {
		// this is a fragment shader
		// TODO
	}

	for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = LLVMGetNextBasicBlock(bb)) {
		for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst != nullptr; inst = LLVMGetNextInstruction(inst)) {
			auto opcode = LLVMGetInstructionOpcode(inst);

			switch (opcode) {
				case LLVMZExt: {
					auto source = LLVMGetOperand(inst, 0);
					auto targetType = LLVMTypeOf(inst);

					auto resID = builder.encodeUConvert(builder.lookupResultID(reinterpret_cast<uintptr_t>(source)), llvmTypeToSPIRVType(builder, targetType));

					builder.associateExistingResultID(resID, reinterpret_cast<uintptr_t>(inst));
				} break;

				// TODO
			}
		}
	}

	builder.endFunction();
};

void Iridium::AIR::Function::emitBody(SPIRV::Builder& builder) {
	std::cout << "function " << _name << std::endl;
	size_t i = 0;

	for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(_function); bb != nullptr; bb = LLVMGetNextBasicBlock(bb)) {
		std::cout << "  block " << i << std::endl;
		++i;
		for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst != nullptr; inst = LLVMGetNextInstruction(inst)) {
			// TEST
			auto tmp = LLVMPrintValueToString(inst);
			std::cout << "    " << tmp << std::endl;
			LLVMDisposeMessage(tmp);
		}
	}
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

void Iridium::AIR::Library::buildModule(SPIRV::Builder& builder) {
	builder.requireCapability(SPIRV::Capability::Shader);
	builder.setAddressingModel(SPIRV::AddressingModel::Logical);
	builder.setMemoryModel(SPIRV::MemoryModel::GLSL450);
	builder.setVersion(1, 3);

	for (auto& [name, func]: _functions) {
		func.analyze(builder);
	}

	for (auto& [name, func]: _functions) {
		func.emitBody(builder);
	}
};
