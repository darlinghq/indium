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
		entryCount != other.entryCount ||
		structureMembers.size() != other.structureMembers.size() ||
		pointerStorageClass != other.pointerStorageClass ||
		targetType != other.targetType ||
		functionReturnType != other.functionReturnType ||
		functionParameterTypes.size() != other.functionParameterTypes.size() ||
		imageDimensionality != other.imageDimensionality ||
		imageDepthIndication != other.imageDepthIndication ||
		imageIsArrayed != other.imageIsArrayed ||
		imageIsMultisampled != other.imageIsMultisampled ||
		imageIsSampledIndication != other.imageIsSampledIndication ||
		imageFormat != other.imageFormat ||
		imageRealSampleTypeID != other.imageRealSampleTypeID
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
	_writer.zeroToAlign(4);
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

	Type typeCopy = type;

	if (typeCopy.backingType == Type::BackingType::Array) {
		declareType(Type(Type::IntegerTag {}, 32, false));
		typeCopy.arrayLengthID = reserveResultID();
	}

	ResultID id = _currentResultID;

	auto [it2, inserted] = _typeIDs.try_emplace(typeCopy, id);
	_reverseTypeIDs.try_emplace(id, &it2->first);
	++_currentResultID;

	if (typeCopy.backingType == Type::BackingType::Matrix) {
		_requiredCapabilities.insert(Capability::Matrix);
	} else if (typeCopy.backingType == Type::BackingType::FloatingPoint) {
		if (typeCopy.scalarWidth < 32) {
			_requiredCapabilities.insert(Capability::Float16);
		} else if (typeCopy.scalarWidth > 32) {
			_requiredCapabilities.insert(Capability::Float64);
		}
	} else if (typeCopy.backingType == Type::BackingType::Integer) {
		if (typeCopy.scalarWidth < 16) {
			_requiredCapabilities.insert(Capability::Int8);
		} else if (typeCopy.scalarWidth < 32) {
			_requiredCapabilities.insert(Capability::Int16);
		} else if (typeCopy.scalarWidth > 32) {
			_requiredCapabilities.insert(Capability::Int64);
		}
	}

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

void Iridium::SPIRV::Builder::setResultType(ResultID result, ResultID type) {
	_resultIDTypes[result] = type;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::lookupResultType(ResultID result) const {
	auto it = _resultIDTypes.find(result);
	return (it == _resultIDTypes.end()) ? ResultIDInvalid : it->second;
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

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalarCommon(uintmax_t value, ResultID typeID, bool usesTwoWords, SpecializationID specializationID) {
	auto key = std::make_pair(value, typeID);

	if (specializationID == SpecializationIDInvalid) {
		auto it = _constantScalars.find(key);

		if (it != _constantScalars.end()) {
			return it->second;
		}
	}

	auto id = reserveResultID();

	_constantScalars.emplace(key, id);

	auto tmp = beginInstruction((specializationID != SpecializationIDInvalid) ? Opcode::SpecConstant : Opcode::Constant, _constants);
	_constants.writeIntegerLE<uint32_t>(typeID);
	_constants.writeIntegerLE<uint32_t>(id);
	_constants.writeIntegerLE<uint32_t>(value & 0xffffffff);
	if (usesTwoWords) {
		_constants.writeIntegerLE<uint32_t>(value >> 32);
	}
	endInstruction(std::move(tmp));

	if (specializationID != SpecializationIDInvalid) {
		addDecoration(id, Decoration { DecorationType::SpecId, { specializationID } });
	}

	return id;
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<uint8_t>(uint8_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 8, false)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<int8_t>(int8_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 8, true)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<uint16_t>(uint16_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 16, false)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<int16_t>(int16_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 16, true)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<uint32_t>(uint32_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 32, false)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<int32_t>(int32_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 32, true)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<uint64_t>(uint64_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 64, false)), true, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<int64_t>(int64_t value, SpecializationID specializationID) {
	return declareConstantScalarCommon(static_cast<uintmax_t>(value), declareType(Type(Type::IntegerTag {}, 64, true)), true, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<const Iridium::Float16*>(const Float16* value, SpecializationID specializationID) {
	// technically UB but it's fine
	uintmax_t tmp = 0;
	memcpy(&tmp, value, sizeof(*value));
	return declareConstantScalarCommon(tmp, declareType(Type(Type::FloatTag {}, 16)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<float>(float value, SpecializationID specializationID) {
	// technically UB but it's fine
	uintmax_t tmp = 0;
	memcpy(&tmp, &value, sizeof(value));
	return declareConstantScalarCommon(tmp, declareType(Type(Type::FloatTag {}, 32)), false, specializationID);
};

template<> Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantScalar<double>(double value, SpecializationID specializationID) {
	// technically UB but it's fine
	uintmax_t tmp = 0;
	memcpy(&tmp, &value, sizeof(value));
	return declareConstantScalarCommon(tmp, declareType(Type(Type::FloatTag {}, 64)), true, specializationID);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareConstantComposite(ResultID typeID, std::vector<ResultID> elements) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::ConstantComposite, _constants);
	_constants.writeIntegerLE<uint32_t>(typeID);
	_constants.writeIntegerLE<uint32_t>(result);
	for (const auto& elm: elements) {
		_constants.writeIntegerLE<uint32_t>(elm);
	}
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareUndefinedValue(ResultID typeID) {
	auto it = _undefinedValues.find(typeID);

	if (it != _undefinedValues.end()) {
		return it->second;
	}

	auto result = reserveResultID();

	auto tmp = beginInstruction(Opcode::Undef, _constants);
	_constants.writeIntegerLE<uint32_t>(typeID);
	_constants.writeIntegerLE<uint32_t>(result);
	endInstruction(std::move(tmp));

	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareNullValue(ResultID typeID) {
	auto it = _nullValues.find(typeID);

	if (it != _nullValues.end()) {
		return it->second;
	}

	auto result = reserveResultID();

	auto tmp = beginInstruction(Opcode::ConstantNull, _constants);
	_constants.writeIntegerLE<uint32_t>(typeID);
	_constants.writeIntegerLE<uint32_t>(result);
	endInstruction(std::move(tmp));

	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::insertLabel(ResultID id) {
	auto tmp = beginInstruction(Opcode::Label, *_currentFunctionWriter);
	if (id == ResultIDInvalid) {
		id = reserveResultID();
	}
	_currentFunctionWriter->writeIntegerLE<uint32_t>(id);
	endInstruction(std::move(tmp));
	return id;
};

void Iridium::SPIRV::Builder::referenceGlobalVariable(ResultID id) {
	_currentFunctionInfo->referencedGlobalVariables.push_back(id);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeUConvert(ResultID typeID, ResultID operand) {
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

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeAccessChain(ResultID resultTypeID, ResultID base, std::vector<ResultID> indices, bool asPointer) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(asPointer ? Opcode::PtrAccessChain : Opcode::AccessChain, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(base);
	for (const auto& index: indices) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(index);
	}
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeLoad(ResultID resultTypeID, ResultID pointer, uint8_t alignment) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::Load, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(pointer);
	if (alignment > 0) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(0x02 /* aligned */);
		_currentFunctionWriter->writeIntegerLE<uint32_t>(alignment);
	}
	endInstruction(std::move(tmp));
	return result;
};

void Iridium::SPIRV::Builder::encodeStore(ResultID pointer, ResultID object, uint8_t alignment) {
	auto tmp = beginInstruction(Opcode::Store, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(pointer);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(object);
	if (alignment > 0) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(0x02 /* aligned */);
		_currentFunctionWriter->writeIntegerLE<uint32_t>(alignment);
	}
	endInstruction(std::move(tmp));
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeConvertUToF(ResultID resultTypeID, ResultID operand) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::ConvertUToF, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeArithUnop(Opcode unop, ResultID resultTypeID, ResultID operand) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(unop, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeArithBinop(Opcode binop, ResultID resultTypeID, ResultID operand1, ResultID operand2) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(binop, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand1);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand2);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeVectorShuffle(ResultID resultTypeID, ResultID vector1, ResultID vector2, std::vector<uint32_t> components) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::VectorShuffle, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(vector1);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(vector2);
	for (const auto& component: components) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(component);
	}
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeCompositeInsert(ResultID resultTypeID, ResultID modifiedPart, ResultID compositeToCopyAndModify, std::vector<uint32_t> indices) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::CompositeInsert, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(modifiedPart);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(compositeToCopyAndModify);
	for (const auto& index: indices) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(index);
	}
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeCompositeExtract(ResultID resultTypeID, ResultID composite, std::vector<uint32_t> indices) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::CompositeExtract, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(composite);
	for (const auto& index: indices) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(index);
	}
	endInstruction(std::move(tmp));
	return result;
};

void Iridium::SPIRV::Builder::encodeReturn(ResultID value) {
	auto tmp = beginInstruction((value == ResultIDInvalid) ? Opcode::Return : Opcode::ReturnValue, *_currentFunctionWriter);
	if (value != ResultIDInvalid) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(value);
	}
	endInstruction(std::move(tmp));
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeConvertPtrToU(ResultID resultTypeID, ResultID target) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::ConvertPtrToU, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(target);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeConvertUToPtr(ResultID resultTypeID, ResultID target) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::ConvertUToPtr, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(target);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeBitcast(ResultID resultTypeID, ResultID operand) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::Bitcast, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeSampledImage(ResultID resultTypeID, ResultID image, ResultID sampler) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::SampledImage, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(image);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(sampler);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeImageSampleImplicitLod(ResultID resultTypeID, ResultID sampledImage, ResultID coordinates) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::ImageSampleImplicitLod, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(sampledImage);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(coordinates);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeFConvert(ResultID resultTypeID, ResultID target) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::FConvert, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(target);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeInverseSqrt(ResultID resultTypeID, ResultID target) {
	auto result = reserveResultID();
	auto tmp = beginGLSLInstruction(GLSLOpcode::InverseSqrt, *_currentFunctionWriter, resultTypeID, result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(target);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeFClamp(ResultID resultTypeID, ResultID target, ResultID min, ResultID max) {
	auto result = reserveResultID();
	auto tmp = beginGLSLInstruction(GLSLOpcode::FClamp, *_currentFunctionWriter, resultTypeID, result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(target);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(min);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(max);
	endInstruction(std::move(tmp));
	return result;
};


Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeFMax(ResultID resultTypeID, ResultID operand1, ResultID operand2) {
	auto result = reserveResultID();
	auto tmp = beginGLSLInstruction(GLSLOpcode::FMax, *_currentFunctionWriter, resultTypeID, result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand1);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand2);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodePow(ResultID resultTypeID, ResultID base, ResultID exponent) {
	auto result = reserveResultID();
	auto tmp = beginGLSLInstruction(GLSLOpcode::Pow, *_currentFunctionWriter, resultTypeID, result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(base);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(exponent);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeVectorExtractDynamic(ResultID resultTypeID, ResultID vector, ResultID index) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::VectorExtractDynamic, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(vector);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(index);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeVectorInsertDynamic(ResultID resultTypeID, ResultID vector, ResultID component, ResultID index) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::VectorInsertDynamic, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(vector);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(component);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(index);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeFOrdLessThan(ResultID operand1, ResultID operand2) {
	auto result = reserveResultID();
	auto resultTypeID = declareType(SPIRV::Type(SPIRV::Type::BooleanTag {}));
	auto tmp = beginInstruction(Opcode::FOrdLessThan, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand1);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand2);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeFOrdGreaterThan(ResultID operand1, ResultID operand2) {
	auto result = reserveResultID();
	auto resultTypeID = declareType(SPIRV::Type(SPIRV::Type::BooleanTag {}));
	auto tmp = beginInstruction(Opcode::FOrdGreaterThan, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand1);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand2);
	endInstruction(std::move(tmp));
	return result;
};

void Iridium::SPIRV::Builder::encodeBranch(ResultID targetLabel) {
	auto tmp = beginInstruction(Opcode::Branch, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(targetLabel);
	endInstruction(std::move(tmp));
};

void Iridium::SPIRV::Builder::encodeBranchConditional(ResultID condition, ResultID trueLabel, ResultID falseLabel) {
	auto tmp = beginInstruction(Opcode::BranchConditional, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(condition);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(trueLabel);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(falseLabel);
	endInstruction(std::move(tmp));
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodePhi(ResultID resultTypeID, std::vector<std::pair<ResultID, ResultID>> variablesAndBlocks) {
	auto result = reserveResultID();
	auto tmp = beginInstruction(Opcode::Phi, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultTypeID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(result);
	for (const auto& [variable, block]: variablesAndBlocks) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(variable);
		_currentFunctionWriter->writeIntegerLE<uint32_t>(block);
	}
	endInstruction(std::move(tmp));
	return result;
};

void Iridium::SPIRV::Builder::encodeSelectionMerge(ResultID mergeBlock) {
	auto tmp = beginInstruction(Opcode::SelectionMerge, *_currentFunctionWriter);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(mergeBlock);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(0);
	endInstruction(std::move(tmp));
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeSqrt(ResultID resultTypeID, ResultID target) {
	auto result = reserveResultID();
	auto tmp = beginGLSLInstruction(GLSLOpcode::Sqrt, *_currentFunctionWriter, resultTypeID, result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(target);
	endInstruction(std::move(tmp));
	return result;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::encodeTrunc(ResultID resultTypeID, ResultID operand) {
	auto result = reserveResultID();
	auto tmp = beginGLSLInstruction(GLSLOpcode::Trunc, *_currentFunctionWriter, resultTypeID, result);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(operand);
	endInstruction(std::move(tmp));
	return result;
};

void Iridium::SPIRV::Builder::encodeDebugPrint(std::string format, std::vector<ResultID> arguments) {
	if (_debugPrintf == ResultIDInvalid) {
		_debugPrintf = reserveResultID();
		_extensions.insert("SPV_KHR_non_semantic_info");
	}

	auto strID = reserveResultID();

	_strings.emplace_back(strID, format);

	auto tmp = beginInstruction(Opcode::ExtInst, *_currentFunctionWriter);
	auto resID = reserveResultID();
	auto resultType = declareType(Type(Type::VoidTag {}));
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resultType);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(resID);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(_debugPrintf);
	_currentFunctionWriter->writeIntegerLE<uint32_t>(1); // debug printf is instruction 1 in the imported instruction set
	_currentFunctionWriter->writeIntegerLE<uint32_t>(strID);
	for (const auto& argument: arguments) {
		_currentFunctionWriter->writeIntegerLE<uint32_t>(argument);
	}
	endInstruction(std::move(tmp));
};

void Iridium::SPIRV::Builder::ensureGLSLExtInstSet() {
	if (_glslExtInstSet == ResultIDInvalid) {
		_glslExtInstSet = reserveResultID();
	}
};

Iridium::SPIRV::Builder::InstructionState Iridium::SPIRV::Builder::beginGLSLInstruction(GLSLOpcode opcode, DynamicByteWriter& writer, ResultID resultTypeID, ResultID resultID) {
	ensureGLSLExtInstSet();
	auto tmp = beginInstruction(Opcode::ExtInst, writer);
	writer.writeIntegerLE<uint32_t>(resultTypeID);
	writer.writeIntegerLE<uint32_t>(resultID);
	writer.writeIntegerLE<uint32_t>(_glslExtInstSet);
	writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(opcode));
	return std::move(tmp);
};

void* Iridium::SPIRV::Builder::finalize(size_t& outputSize) {
	InstructionState tmp;

	for (auto& entryPoint: _entryPoints) {
		auto info = _functionInfos[entryPoint.functionID];
		// TODO: when we add support for function calls, we'll need to keep track of which functions the entry points call (even indirectly),
		//       so that we can obtain the full list of referenced global variables.
		entryPoint.referencedGlobalVariables.insert(entryPoint.referencedGlobalVariables.end(), info.referencedGlobalVariables.begin(), info.referencedGlobalVariables.end());
	}

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

	// emit extensions
	for (const auto& extension: _extensions) {
		tmp = beginInstruction(Opcode::Extension, _writer);
		encodeString(extension);
		endInstruction(std::move(tmp));
	}

	// emit imported extension instructions
	if (_debugPrintf != ResultIDInvalid) {
		tmp = beginInstruction(Opcode::ExtInstImport, _writer);
		_writer.writeIntegerLE<uint32_t>(_debugPrintf);
		encodeString("NonSemantic.DebugPrintf");
		endInstruction(std::move(tmp));
	}

	if (_glslExtInstSet != ResultIDInvalid) {
		tmp = beginInstruction(Opcode::ExtInstImport, _writer);
		_writer.writeIntegerLE<uint32_t>(_glslExtInstSet);
		encodeString("GLSL.std.450");
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

	// now emit execution modes
	// TODO: allow this to be configured
	for (const auto& entryPoint: _entryPoints) {
		for (const auto& mode: entryPoint.executionModes) {
			tmp = beginInstruction(mode.requiresIdOperands ? Opcode::ExecutionModeId : Opcode::ExecutionMode, _writer);
			_writer.writeIntegerLE<uint32_t>(entryPoint.functionID);
			_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(mode.mode));
			for (const auto& arg: mode.arguments) {
				_writer.writeIntegerLE<uint32_t>(arg);
			}
			endInstruction(std::move(tmp));
		}
		if (entryPoint.executionModel == ExecutionModel::Fragment) {
			tmp = beginInstruction(Opcode::ExecutionMode, _writer);
			_writer.writeIntegerLE<uint32_t>(entryPoint.functionID);
			_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(ExecutionMode::OriginUpperLeft));
			endInstruction(std::move(tmp));
		}
	}

	// emit strings
	for (const auto& [id, string]: _strings) {
		tmp = beginInstruction(Opcode::String, _writer);
		_writer.writeIntegerLE<uint32_t>(id);
		encodeString(string);
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
			tmp = beginInstruction(Opcode::MemberDecorate, _writer);
			_writer.writeIntegerLE<uint32_t>(id);
			_writer.writeIntegerLE<uint32_t>(i);
			_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(DecorationType::Offset));
			_writer.writeIntegerLE<uint32_t>(member.offset);
			endInstruction(std::move(tmp));
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

	// now emit automatic type decorations
	for (const auto& [type, id]: _typeIDs) {
		if (type.backingType == Type::BackingType::RuntimeArray || type.backingType == Type::BackingType::Array) {
			auto elmTypeInst = _reverseTypeIDs[type.targetType];
			tmp = beginInstruction(Opcode::Decorate, _writer);
			_writer.writeIntegerLE<uint32_t>(id);
			_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(DecorationType::ArrayStride));
			// align the element size to a multiple of its alignment
			_writer.writeIntegerLE<uint32_t>((elmTypeInst->size + (elmTypeInst->alignment - 1)) & ~(elmTypeInst->alignment - 1));
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

			case Type::BackingType::Array: {
				auto intTypeInst = Type(Type::IntegerTag {}, 32, false);
				emitType(intTypeInst, lookupType(intTypeInst));
			} /* fallthrough */

			case Type::BackingType::Pointer: /* fallthrough */
			case Type::BackingType::Vector: /* fallthrough */
			case Type::BackingType::Matrix: /* fallthrough */
			case Type::BackingType::RuntimeArray: /* fallthrough */
			case Type::BackingType::Image: /* fallthrough */
			case Type::BackingType::SampledImage:
				emitType(*reverseLookupType(type.targetType), type.targetType);
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
				_writer.writeIntegerLE<uint32_t>(type.targetType);
				_writer.writeIntegerLE<uint32_t>(type.entryCount);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Matrix:
				tmp = beginInstruction(Opcode::TypeMatrix, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.targetType);
				_writer.writeIntegerLE<uint32_t>(type.entryCount);
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
				_writer.writeIntegerLE<uint32_t>(type.targetType);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::RuntimeArray:
				tmp = beginInstruction(Opcode::TypeRuntimeArray, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.targetType);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Image:
				tmp = beginInstruction(Opcode::TypeImage, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.targetType);
				_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(type.imageDimensionality));
				_writer.writeIntegerLE<uint32_t>(type.imageDepthIndication);
				_writer.writeIntegerLE<uint32_t>(type.imageIsArrayed ? 1 : 0);
				_writer.writeIntegerLE<uint32_t>(type.imageIsMultisampled ? 1 : 0);
				_writer.writeIntegerLE<uint32_t>(type.imageIsSampledIndication);
				_writer.writeIntegerLE<uint32_t>(static_cast<uint32_t>(type.imageFormat));
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Sampler:
				tmp = beginInstruction(Opcode::TypeSampler, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::SampledImage:
				tmp = beginInstruction(Opcode::TypeSampledImage, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.targetType);
				endInstruction(std::move(tmp));
				break;
			case Type::BackingType::Array: {
				auto intTypeInst = Type(Type::IntegerTag {}, 32, false);
				auto intType = lookupType(intTypeInst);
				tmp = beginInstruction(Opcode::Constant, _writer);
				_writer.writeIntegerLE<uint32_t>(intType);
				_writer.writeIntegerLE<uint32_t>(type.arrayLengthID);
				_writer.writeIntegerLE<uint32_t>(type.entryCount);
				endInstruction(std::move(tmp));
				tmp = beginInstruction(Opcode::TypeArray, _writer);
				_writer.writeIntegerLE<uint32_t>(id);
				_writer.writeIntegerLE<uint32_t>(type.targetType);
				_writer.writeIntegerLE<uint32_t>(type.arrayLengthID);
				endInstruction(std::move(tmp));
			} break;
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
