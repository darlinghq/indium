#pragma once

#include <cstddef>

#include <iridium/bits.hpp>

#include <vector>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace Iridium {
	namespace SPIRV {
		enum class MemoryModel: uint32_t {
			Simple = 0,
			GLSL450 = 1,
		};

		enum class AddressingModel: uint32_t {
			Logical = 0,
		};

		enum class ExecutionModel: uint32_t {
			Vertex = 0,
			Fragment = 4,
		};

		enum class DecorationType: uint32_t {
			Block = 2,
			ArrayStride = 6,
			Builtin = 11,
			NonWritable = 24,
			Location = 30,
			Binding = 33,
			DescriptorSet = 34,
			Offset = 35,
		};

		enum class BuiltinID: uint32_t {
			Position = 0,
			VertexID = 5,
			VertexIndex = 42,
		};

		enum class StorageClass: uint32_t {
			UniformConstant = 0,
			Input = 1,
			Uniform = 2,
			Output = 3,
			StorageBuffer = 12,
		};

		struct Decoration {
			DecorationType type;
			std::vector<uint32_t> arguments;

			bool operator==(const Decoration& other) const;

			bool operator!=(const Decoration& other) const {
				return !(*this == other);
			};
		};
	};
};

template<>
struct std::hash<Iridium::SPIRV::Decoration> {
	size_t operator()(const Iridium::SPIRV::Decoration& decoration) const {
		size_t result = std::hash<Iridium::SPIRV::DecorationType>()(decoration.type);
		for (const auto& argument: decoration.arguments) {
			result = ((result << 1) ^ std::hash<uint32_t>()(argument)) >> 1;
		}
		return result;
	};
};


namespace Iridium {
	namespace SPIRV {
		using ResultID = uint32_t;

		struct EntryPoint {
			ExecutionModel executionModel;
			ResultID functionID;
			std::string name;
			std::vector<ResultID> referencedGlobalVariables;
		};

		struct Type {
			// not an enum class on purpose
			enum BackingType {
				Void,
				Boolean,
				Integer,
				FloatingPoint,
				Structure,
			};

			struct Member;

			BackingType backingType = BackingType::Structure;
			size_t rowCount = 0;
			size_t columnCount = 0;
			std::vector<Member> members;
			StorageClass pointerStorageClass;
			bool isPointer = false;

			Type() {};
			explicit Type(BackingType _backingType, size_t _rowCount = 0, size_t _columnCount = 0):
				backingType(_backingType),
				rowCount(_rowCount),
				columnCount(_columnCount)
				{};

			bool operator==(const Type& other) const;

			bool operator!=(const Type& other) const {
				return !(*this == other);
			};
		};

		struct Type::Member {
			Type type;
			std::unordered_set<Decoration> decorations;

			bool operator==(const Member& other) const;

			bool operator!=(const Member& other) const {
				return !(*this == other);
			};
		};
	};
};

template<>
struct std::hash<Iridium::SPIRV::Type> {
	size_t operator()(const Iridium::SPIRV::Type& type) const {
		size_t result = std::hash<Iridium::SPIRV::Type::BackingType>()(type.backingType);
		result = ((result << 1) ^ std::hash<size_t>()(type.rowCount)) >> 1;
		result = ((result << 1) ^ std::hash<size_t>()(type.columnCount)) >> 1;
		for (const auto& member: type.members) {
			result = ((result << 1) ^ std::hash<Iridium::SPIRV::Type>()(member.type)) >> 1;
			for (const auto& decoration: member.decorations) {
				result = ((result << 1) ^ std::hash<Iridium::SPIRV::Decoration>()(decoration)) >> 1;
			}
		}
		return result;
	};
};

namespace Iridium {
	namespace SPIRV {
		struct GlobalVariable {
			StorageClass storageClass;
			Type type;
		};

		class Builder {
		private:
			DynamicByteWriter _writer;
			std::unordered_map<Type, ResultID> _typeIDs;
			ResultID _currentResultID = 0;
			std::vector<EntryPoint> _entryPoints;
			std::unordered_map<uintptr_t, ResultID> _associatedIDs;
			std::unordered_map<ResultID, std::unordered_set<Decoration>> _decorations;
			std::unordered_map<ResultID, GlobalVariable> _globalVariables;

			struct InstructionState {
				size_t position;
				uint16_t opcode;
			};

			void encodeInstructionHeader(uint16_t wordCount, uint16_t opcode);
			void encodeString(std::string_view string);
			InstructionState beginInstruction(uint16_t opcode);
			void endInstruction(InstructionState&& state);

		public:
			Builder();

			ResultID declareType(const Type& type);
			ResultID lookupType(const Type& type) const;

			void setMemoryModel(MemoryModel memoryModel);

			ResultID reserveResultID();
			ResultID associateResultID(uintptr_t association);
			ResultID lookupResultID(uintptr_t association) const;

			void addEntryPoint(const EntryPoint& entryPoint);
			ResultID addGlobalVariable(const Type& type, StorageClass storageClass);
			void addDecoration(ResultID resultID, const Decoration& decoration);


			void emitHeader();

			void* finalize(size_t& outputSize);
		};
	};
};
