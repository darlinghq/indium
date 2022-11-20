#include <iridium/spirv.hpp>
#include <optional>
#include <functional>

bool Iridium::SPIRV::Decoration::operator==(const Decoration& other) const {
	if (type != other.type || arguments.size() != other.arguments.size()) {
		return false;
	}
	for (size_t i = 0; i < arguments.size(); ++i) {
		if (arguments[i] != other.arguments[i]) {
			return false;
		}
	}
	return true;
};

bool Iridium::SPIRV::Type::operator==(const Type& other) const {
	if (
		backingType != other.backingType ||
		scalarWidth != other.scalarWidth ||
		integerIsSigned != other.integerIsSigned ||
		vectorMatrixEntryCount != other.vectorMatrixEntryCount ||
		structureMembers.size() != other.structureMembers.size() ||
		pointerStorageClass != other.pointerStorageClass ||
		pointerVectorMatrixArrayTargetType != other.pointerVectorMatrixArrayTargetType ||
		functionReturnType != other.functionReturnType ||
		functionParameterTypes.size() != other.functionParameterTypes.size()
	) {
		return false;
	}
	for (size_t i = 0; i < structureMembers.size(); ++i) {
		if (structureMembers[i] != other.structureMembers[i]) {
			return false;
		}
	}
	for (size_t i = 0; i < functionParameterTypes.size(); ++i) {
		if (functionParameterTypes[i] != other.functionParameterTypes[i]) {
			return false;
		}
	}
	return true;
};

bool Iridium::SPIRV::Type::Member::operator==(const Member& other) const {
	if (id != other.id || decorations.size() != other.decorations.size()) {
		return false;
	}
	for (const auto& decoration: decorations) {
		if (other.decorations.find(decoration) == other.decorations.end()) {
			return false;
		}
	}
	for (const auto& decoration: other.decorations) {
		if (decorations.find(decoration) == decorations.end()) {
			return false;
		}
	}
	return true;
};

Iridium::SPIRV::Builder::Builder() {};

void Iridium::SPIRV::Builder::encodeInstructionHeader(DynamicByteWriter& writer, uint16_t wordCount, uint16_t opcode) {
	writer.writeIntegerLE<uint32_t>((static_cast<uint32_t>(wordCount) << 16) | static_cast<uint32_t>(opcode));
};

void Iridium::SPIRV::Builder::encodeString(std::string_view string) {
	auto written = _writer.writeString(string, true);
	auto padded = roundUpPowerOf2<size_t>(written, 4);
	if (padded > written) {
		_writer.zero(padded - written);
	}
};

Iridium::SPIRV::Builder::InstructionState Iridium::SPIRV::Builder::beginInstruction(uint16_t opcode, DynamicByteWriter& writer) {
	auto offset = writer.offset();
	writer.resizeAndSkip(sizeof(uint32_t));
	return {
		offset,
		opcode,
		&writer,
	};
};

Iridium::SPIRV::Builder::InstructionState Iridium::SPIRV::Builder::beginInstruction(Opcode opcode, DynamicByteWriter& writer) {
	return beginInstruction(static_cast<uint16_t>(opcode), writer);
};

void Iridium::SPIRV::Builder::endInstruction(InstructionState&& state) {
	auto curr = state.writer->offset();
	state.writer->seek(state.position);
	encodeInstructionHeader(*state.writer, (curr - state.position) / sizeof(uint32_t), state.opcode);
	state.writer->seek(curr);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareType(const Type& type) {
	auto it = _typeIDs.find(type);

	if (it != _typeIDs.end()) {
		return it->second;
	}

	ResultID id = _currentResultID;

	auto [it2, inserted] = _typeIDs.try_emplace(type, id);
	_reverseTypeIDs.try_emplace(id, &it2->first);
	++_currentResultID;

	return id;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::lookupType(const Type& type) const {
	auto it = _typeIDs.find(type);
	if (it == _typeIDs.end()) {
		//throw std::out_of_range("No such type present in type ID map");
		return ResultIDInvalid;
	}
	return it->second;
};

std::optional<Iridium::SPIRV::Type> Iridium::SPIRV::Builder::reverseLookupType(ResultID id) const {
	auto it = _reverseTypeIDs.find(id);
	if (it == _reverseTypeIDs.end()) {
		return std::nullopt;
	}
	return *it->second;
};

void Iridium::SPIRV::Builder::setAddressingModel(AddressingModel addressingModel) {
	_addressingModel = addressingModel;
};

void Iridium::SPIRV::Builder::setMemoryModel(MemoryModel memoryModel) {
	_memoryModel = memoryModel;
};

void Iridium::SPIRV::Builder::setVersion(uint8_t major, uint8_t minor) {
	_versionMajor = major;
	_versionMinor = minor;
};

void Iridium::SPIRV::Builder::setGeneratorID(uint16_t id, uint16_t version) {
	_generatorID = id;
	_generatorVersion = version;
};

void Iridium::SPIRV::Builder::requireCapability(Capability capability) {
	_requiredCapabilities.insert(capability);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::reserveResultID() {
	auto id = _currentResultID;
	++_currentResultID;
	return id;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::associateResultID(uintptr_t association) {
	auto it = _associatedIDs.find(association);

	if (it != _associatedIDs.end()) {
		return it->second;
	}

	ResultID id = _currentResultID;

	_associatedIDs.try_emplace(association, _currentResultID);
	++_currentResultID;

	return id;
};

void Iridium::SPIRV::Builder::associateExistingResultID(ResultID id, uintptr_t association) {
	_associatedIDs.try_emplace(association, id);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::lookupResultID(uintptr_t association) const {
	auto it = _associatedIDs.find(association);
	if (it == _associatedIDs.end()) {
		//throw std::out_of_range("No such association present in associated ID map");
		return ResultIDInvalid;
	}
	return it->second;
};

void Iridium::SPIRV::Builder::addEntryPoint(const EntryPoint& entryPoint) {
	_entryPoints.push_back(entryPoint);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::addGlobalVariable(ResultID typeID, StorageClass storageClass) {
	auto resultID = reserveResultID();
	_globalVariables[resultID] = GlobalVariable { storageClass, typeID };
	return resultID;
};

void Iridium::SPIRV::Builder::addDecoration(ResultID resultID, const Decoration& decoration) {
	if (_decorations.find(resultID) == _decorations.end()) {
		_decorations[resultID] = std::unordered_set<Decoration> { decoration };
	} else {
		_decorations[resultID].insert(decoration);
	}
};

Iridium::SPIRV::FunctionInfo Iridium::SPIRV::Builder::declareFunction(ResultID functionType) {
	auto funcType = *reverseLookupType(functionType);
	auto id = reserveResultID();
	std::vector<ResultID> paramIDs;
	for (size_t i = 0; i < funcType.functionParameterTypes.size(); ++i) {
		paramIDs.push_back(reserveResultID());
	}
	auto funcInfo = PrivateFunctionInfo {
		FunctionInfo {
			id,
			std::move(paramIDs),
		},
		functionType,
		ResultIDInvalid,
	};
	_functionInfos[id] = funcInfo;
	return funcInfo.idInfo;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::beginFunction(ResultID id) {
	auto it = _functionInfos.find(id);

	if (it == _functionInfos.end()) {
		return ResultIDInvalid;
	}

	auto it2 = _functionWriters.find(id);

	if (it2 != _functionWriters.end()) {
		return ResultIDInvalid;
	}

	auto [it3, inserted] = _functionWriters.try_emplace(id);

	_currentFunctionInfo = &it->second;
	_currentFunctionWriter = &it3->second;

	it->second.firstLabel = reserveResultID();

	return it->second.firstLabel;
};

void Iridium::SPIRV::Builder::endFunction() {
	_currentFunctionWriter = nullptr;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::insertLabel() {
	auto tmp = beginInstruction(Opcode::Label, *_currentFunctionWriter);
	auto id = reserveResultID();
	_currentFunctionWriter->writeIntegerLE<uint32_t>(id);
	endInstruction(std::move(tmp));
	return id;
};

void Iridium::SPIRV::Builder::referenceGlobalVariable(ResultID id) {
	// TODO: keep track of which functions reference which global variables,
	//       and use this info to determine which global variables are referenced by each entry point.
	//       this is necessary because each entry point needs to reference a superset of all the global variables
	//       used by it, including any used in functions it calls.
	//
	// for now, we just add the global variable reference to all entry points. not efficient, but it's spec-compliant.

	for (auto& entryPoint: _entryPoints) {
		entryPoint.referencedGlobalVariables.push_back(id);
	}
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeUConvert(ResultID operand, ResultID typeID) {
	auto result = reserveResultID();
	auto type = *reverseLookupType(typeID);
	type.integerIsSigned = false;
	typeID = declareType(type);
	auto tmp = beginInstruction(Opcode::UConvert, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(typeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand);
	endInstruction(std::move(tmp));
	return result;
};

void* Iridium::SPIRV::Builder::finalize(size_t& outputSize) {
	InstructionState tmp;

	// emit SPIR-V magic number
	_writer.writeIntegerLE<uint32_t>(0x07230203);

	// emit version number
	_writer.writeIntegerLE<uint32_t>(((uint32_t)_versionMajor << 16) | ((uint32_t)_versionMinor << 8));

	// emit generator info
	_writer.writeIntegerLE<uint32_t>(((uint32_t)_generatorID << 16) | _generatorVersion);

	// emit ID bound
	// (`_currentResultID` is the next ID to use, so it's our final bound)
	_writer.writeIntegerLE<uint32_t>(_currentResultID);

	// emit 0 (reserved)
	_writer.writeIntegerLE<uint32_t>(0);

	// emit capabilities
	for (const auto& capability: _requiredCapabilities) {
		tmp = beginInstruction(Opcode::Capability, _writer);
		_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(capability));
		endInstruction(std::move(tmp));
	}

	// emit the memory model
	tmp = beginInstruction(Opcode::MemoryModel, _writer);
	_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(_addressingModel));
	_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(_memoryModel));
	endInstruction(std::move(tmp));

	// now emit entry points
	for (const auto& entryPoint: _entryPoints) {
		tmp = beginInstruction(Opcode::EntryPoint, _writer);
		_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(entryPoint.executionModel));
		_writer.writeIntegerLE<uint32_t>(entryPoint.functionID);
		encodeString(entryPoint.name);
		for (const auto& referencedGlobalVariable: entryPoint.referencedGlobalVariables) {
			_writer.writeIntegerLE<uint32_t>(referencedGlobalVariable);
		}
		endInstruction(std::move(tmp));
	}

	// emit type member decorations
	for (const auto& [type, id]: _typeIDs) {
		for (size_t i = 0; i < type.structureMembers.size(); ++i) {
			const auto& member = type.structureMembers[i];
			for (const auto& decoration: member.decorations) {
				tmp = beginInstruction(Opcode::MemberDecorate, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(i);
				_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(decoration.type));
				for (const auto& decorationArgument: decoration.arguments) {
					_writer.writeIntegerLE<uint32_t>(decorationArgument);
				}
				endInstruction(std::move(tmp));
			}
		}
	}

	// now emit general decorations
	for (const auto& [id, decorations]: _decorations) {
		for (const auto& decoration: decorations) {
			tmp = beginInstruction(Opcode::Decorate, _writer);
			_writer.writeIntegerLE<uint32_t>(id);
			_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(decoration.type));
			for (const auto& decorationArgument: decoration.arguments) {
				_writer.writeIntegerLE<uint32_t>(decorationArgument);
			}
			endInstruction(std::move(tmp));
		}
	}

	// now emit types
	std::unordered_set<ResultID> emittedTypes;
	std::function<void(const Type&, ResultID)> emitType = [&](const Type& type, ResultID id) {
		if (emittedTypes.find(id) != emittedTypes.end()) {
			return;
		}

		emittedTypes.insert(id);

		// emit types we depend on first
		switch (type.backingType) {
			// no dependencies
			case Type::BackingType::Void:
			case Type::BackingType::Boolean:
			case Type::BackingType::Integer:
			case Type::BackingType::FloatingPoint:
				break;

			case Type::BackingType::Structure:
				for (const auto& member: type.structureMembers) {
					emitType(*reverseLookupType(member.id), member.id);
				}
				break;

			case Type::BackingType::Pointer: /* fallthrough */
			case Type::BackingType::Vector: /* fallthrough */
			case Type::BackingType::Matrix: /* fallthrough */
			case Type::BackingType::RuntimeArray:
				emitType(*reverseLookupType(type.pointerVectorMatrixArrayTargetType), type.pointerVectorMatrixArrayTargetType);
				break;

			case Type::BackingType::Function:
				emitType(*reverseLookupType(type.functionReturnType), type.functionReturnType);
				for (const auto& paramID: type.functionParameterTypes) {
					emitType(*reverseLookupType(paramID), paramID);
				}
				break;

			default:
				break;
		}

		// now emit the type itself
		switch (type.backingType) {
			case Type::BackingType::Void:
				tmp = beginInstruction(Opcode::TypeVoid, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Boolean:
				tmp = beginInstruction(Opcode::TypeBool, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Integer:
				tmp = beginInstruction(Opcode::TypeInt, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.scalarWidth);
				_writer.writeIntegerLE<uint32_t>(type.integerIsSigned ? 1 : 0);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::FloatingPoint:
				tmp = beginInstruction(Opcode::TypeFloat, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.scalarWidth);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Structure:
				tmp = beginInstruction(Opcode::TypeStruct, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				for (const auto& member: type.structureMembers) {
					_writer.writeIntegerLE<uint32_t>(member.id);
				}
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Vector:
				tmp = beginInstruction(Opcode::TypeVector, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.pointerVectorMatrixArrayTargetType);
				_writer.writeIntegerLE<uint32_t>(type.vectorMatrixEntryCount);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Matrix:
				tmp = beginInstruction(Opcode::TypeMatrix, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.pointerVectorMatrixArrayTargetType);
				_writer.writeIntegerLE<uint32_t>(type.vectorMatrixEntryCount);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Function:
				tmp = beginInstruction(Opcode::TypeFunction, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.functionReturnType);
				for (const auto& paramTypeID: type.functionParameterTypes) {
					_writer.writeIntegerLE<uint32_t>(paramTypeID);
				}
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Pointer:
				tmp = beginInstruction(Opcode::TypePointer, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(type.pointerStorageClass));
				_writer.writeIntegerLE<uint32_t>(type.pointerVectorMatrixArrayTargetType);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::RuntimeArray:
				tmp = beginInstruction(Opcode::TypeRuntimeArray, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.pointerVectorMatrixArrayTargetType);
				endInstruction(std::move(tmp));
				break;
			default:
				break;
		}
	};
	for (const auto& [type, id]: _typeIDs) {
		emitType(type, id);
	}

	// now emit constants
	auto constantsBlockLength = _constants.size();
	auto constantsBlock = _constants.extractBuffer();
	_writer.writeRaw(constantsBlock, constantsBlockLength);
	free(constantsBlock);

	// now emit global variable declarations
	for (const auto& [id, var]: _globalVariables) {
		tmp = beginInstruction(Opcode::Variable, _writer);
		_writer.writeIntegerLE<uint32_t>(var.typeID);
		_writer.writeIntegerLE<uint32_t>(id);
		_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(var.storageClass));
		// TODO: support initializers
		endInstruction(std::move(tmp));
	}

	auto emitFunctionHeader = [&](const PrivateFunctionInfo& funcInfo) {
		auto funcType = *reverseLookupType(funcInfo.functionType);

		tmp = beginInstruction(Opcode::Function, _writer);
		_writer.writeIntegerLE<uint32_t>(funcType.functionReturnType);
		_writer.writeIntegerLE<uint32_t>(funcInfo.idInfo.id);
		_writer.writeIntegerLE<uint32_t>(0); // TODO: support function control bits
		_writer.writeIntegerLE<uint32_t>(funcInfo.functionType);
		endInstruction(std::move(tmp));

		for (size_t i = 0; i < funcType.functionParameterTypes.size(); ++i) {
			const auto& paramID = funcInfo.idInfo.parameterIDs[i];
			const auto& paramTypeID = funcType.functionParameterTypes[i];

			tmp = beginInstruction(Opcode::FunctionParameter, _writer);
			_writer.writeIntegerLE<uint32_t>(paramTypeID);
			_writer.writeIntegerLE<uint32_t>(paramID);
			endInstruction(std::move(tmp));
		}
	};

	// emit function declarations (functions without a body)
	for (const auto& [id, funcInfo]: _functionInfos) {
		if (_functionWriters.find(id) != _functionWriters.end()) {
			// this function has a writer, so it has a body
			continue;
		}

		emitFunctionHeader(funcInfo);

		tmp = beginInstruction(Opcode::FunctionEnd, _writer);
		endInstruction(std::move(tmp));
	}

	// finally, emit function definitions (functions with a body)
	for (const auto& [id, funcInfo]: _functionInfos) {
		if (_functionWriters.find(id) == _functionWriters.end()) {
			// this function doesn't have a writer, so it has no body
			continue;
		}

		emitFunctionHeader(funcInfo);

		// emit the first label
		tmp = beginInstruction(Opcode::Label, _writer);
		_writer.writeIntegerLE<uint32_t>(funcInfo.firstLabel);
		endInstruction(std::move(tmp));

		// emit variables
		for (const auto& [id, variable]: funcInfo.variables) {
			tmp = beginInstruction(Opcode::Variable, _writer);
			_writer.writeIntegerLE<uint32_t>(variable.typeID);
			_writer.writeIntegerLE<uint32_t>(id);
			_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(StorageClass::Function));
			if (variable.initializer != ResultIDInvalid) {
				_writer.writeIntegerLE<uint32_t>(variable.initializer);
			}
			endInstruction(std::move(tmp));
		}

		// emit the actual function body
		auto& writer = _functionWriters[id];
		auto length = writer.size();
		auto buf = writer.extractBuffer();
		_writer.writeRaw(buf, length);
		free(buf);

		tmp = beginInstruction(Opcode::FunctionEnd, _writer);
		endInstruction(std::move(tmp));
	}

	// return final output size and extracted buffer
	outputSize = _writer.size();
	return _writer.extractBuffer();
};
