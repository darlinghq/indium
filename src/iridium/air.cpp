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

void Iridium::AIR::Function::analyze(SPIRV::Builder& builder) const {
	// TODO
};

// TEST
#include <iostream>

void Iridium::AIR::Function::emitBody(SPIRV::Builder& builder) const {
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

void Iridium::AIR::Library::buildModule(SPIRV::Builder& builder) const {
	for (const auto& [name, func]: _functions) {
		func.analyze(builder);
	}



	for (const auto& [name, func]: _functions) {
		func.emitBody(builder);
	}
};
