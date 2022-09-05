#include <iridium/spirv.hpp>

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
	if (backingType != other.backingType || rowCount != other.rowCount || columnCount != other.columnCount || members.size() != other.members.size()) {
		return false;
	}
	for (size_t i = 0; i < members.size(); ++i) {
		if (members[i] != other.members[i]) {
			return false;
		}
	}
	return true;
};

bool Iridium::SPIRV::Type::Member::operator==(const Member& other) const {
	if (type != other.type || decorations.size() != other.decorations.size()) {
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

Iridium::SPIRV::Builder::Builder() {
	// SPIR-V magic number
	_writer.writeIntegerLE<uint32_t>(0x07230203);
};

void Iridium::SPIRV::Builder::encodeInstructionHeader(uint16_t wordCount, uint16_t opcode) {
	_writer.writeIntegerLE<uint32_t>((static_cast<uint32_t>(wordCount) << 16) | static_cast<uint32_t>(opcode));
};

void Iridium::SPIRV::Builder::encodeString(std::string_view string) {
	auto written = _writer.writeString(string, true);
	auto padded = roundUpPowerOf2<size_t>(written, 4);
	if (padded > written) {
		_writer.zero(padded - written);
	}
};

Iridium::SPIRV::Builder::InstructionState Iridium::SPIRV::Builder::beginInstruction(uint16_t opcode) {
	auto offset = _writer.offset();
	_writer.skip(sizeof(uint32_t));
	return {
		offset,
		opcode,
	};
};

void Iridium::SPIRV::Builder::endInstruction(InstructionState&& state) {
	auto curr = _writer.offset();
	_writer.seek(state.position);
	encodeInstructionHeader((curr - state.position) / sizeof(uint32_t), state.opcode);
	_writer.seek(curr);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::declareType(const Type& type) {
	auto it = _typeIDs.find(type);

	if (it != _typeIDs.end()) {
		return it->second;
	}

	for (const auto& member: type.members) {
		declareType(member.type);
	}

	ResultID id = _currentResultID;

	_typeIDs.try_emplace(type, _currentResultID);
	++_currentResultID;

	return id;
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::lookupType(const Type& type) const {
	auto it = _typeIDs.find(type);
	if (it == _typeIDs.end()) {
		throw std::out_of_range("No such type present in type ID map");
	}
	return it->second;
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

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::lookupResultID(uintptr_t association) const {
	auto it = _associatedIDs.find(association);
	if (it == _associatedIDs.end()) {
		throw std::out_of_range("No such association present in associated ID map");
	}
	return it->second;
};

void Iridium::SPIRV::Builder::addEntryPoint(const EntryPoint& entryPoint) {
	_entryPoints.push_back(entryPoint);
};

Iridium::SPIRV::ResultID Iridium::SPIRV::Builder::addGlobalVariable(const Type& type, StorageClass storageClass) {
	// TODO
};

void Iridium::SPIRV::Builder::addDecoration(ResultID resultID, const Decoration& decoration) {
	// TODO
};

void Iridium::SPIRV::Builder::emitHeader() {
	// TODO

	// now emit entry points
	for (const auto& entryPoint: _entryPoints) {
		// TODO
	}

	// finalize type decorations
	for (const auto& [type, id]: _typeIDs) {
		// TODO
	}

	// now emit decorations
	// TODO

	// now emit types
	for (const auto& [type, id]: _typeIDs) {
		// TODO
	}
};

void* Iridium::SPIRV::Builder::finalize(size_t& outputSize) {
	outputSize = _writer.size();
	return _writer.extractBuffer();
};
