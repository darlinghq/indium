#pragma once

#include <cstddef>

#include <iridium/bits.hpp>

#include <vector>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace Iridium {
	namespace SPIRV {
		enum class MemoryModel: uint32_t {
			Simple = 0,
			GLSL450 = 1,
		};

		enum class AddressingModel: uint32_t {
			Logical = 0,
			Physical32 = 1,
			Physical64 = 2,
			PhysicalStorageBuffer64 = 5348,
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
			PointSize = 1,
			ClipDistance = 3,
			CullDistance = 4,
			VertexID = 5,
			VertexIndex = 42,
		};

		enum class StorageClass: uint32_t {
			UniformConstant = 0,
			Input = 1,
			Uniform = 2,
			Output = 3,
			Function = 7,
			StorageBuffer = 12,
			PhysicalStorageBuffer = 5349,
		};

		enum class Capability: uint32_t {
			Matrix = 0,
			Shader = 1,
			Addresses = 4,
			Float16 = 9,
			Float64 = 10,
			Int64 = 11,
			Int16 = 22,
			Int8 = 39,
			PhysicalStorageBufferAddresses = 5347,
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
		constexpr ResultID ResultIDInvalid = 0;

		struct EntryPoint {
			ExecutionModel executionModel;
			ResultID functionID;
			std::string name;
			std::vector<ResultID> referencedGlobalVariables;
		};

		struct Type {
			// not an enum class on purpose
			enum class BackingType {
				Void,
				Boolean,
				Integer,
				FloatingPoint,
				Structure,
				Vector,
				Matrix,
				Function,
				Pointer,
				RuntimeArray,
			};

			struct VoidTag {};
			struct BooleanTag {};
			struct IntegerTag {};
			struct FloatTag {};
			struct StructureTag {};
			struct VectorTag {};
			struct MatrixTag {};
			struct FunctionTag {};
			struct PointerTag {};
			struct RuntimeArrayTag {};

			struct Member;

			BackingType backingType = BackingType::Structure;

			size_t scalarWidth = 0;

			bool integerIsSigned = false;

			size_t vectorMatrixEntryCount = 0;
			size_t size = 0;
			size_t alignment = 1;

			std::vector<Member> structureMembers;

			StorageClass pointerStorageClass = StorageClass::UniformConstant;

			ResultID pointerVectorMatrixArrayTargetType = ResultIDInvalid;

			ResultID functionReturnType = ResultIDInvalid;
			std::vector<ResultID> functionParameterTypes;

			Type() {};
			explicit Type(const VoidTag&):
				backingType(BackingType::Void)
				{};
			explicit Type(const BooleanTag&):
				backingType(BackingType::Boolean)
				{};
			explicit Type(const IntegerTag&, size_t width, bool isSigned):
				backingType(BackingType::Integer),
				scalarWidth(width),
				integerIsSigned(isSigned),
				size((width + 7) / 8),
				alignment(size)
				{};
			explicit Type(const FloatTag&, size_t width):
				backingType(BackingType::FloatingPoint),
				scalarWidth(width),
				size((width + 7) / 8),
				alignment(size)
				{};
			explicit Type(const StructureTag&, std::vector<Member> members, size_t totalSize, size_t structAlignment):
				backingType(BackingType::Structure),
				structureMembers(members),
				size(totalSize),
				alignment(structAlignment)
				{};
			explicit Type(const VectorTag&, size_t componentCount, ResultID componentType, size_t totalSize, size_t vectorAlignment):
				backingType(BackingType::Vector),
				vectorMatrixEntryCount(componentCount),
				pointerVectorMatrixArrayTargetType(componentType),
				size(totalSize),
				alignment(vectorAlignment)
				{};
			explicit Type(const MatrixTag&, size_t columnCount, ResultID entryType, size_t totalSize, size_t matrixAlignment):
				backingType(BackingType::Matrix),
				vectorMatrixEntryCount(columnCount),
				pointerVectorMatrixArrayTargetType(entryType),
				size(totalSize),
				alignment(matrixAlignment)
				{};
			explicit Type(const FunctionTag&, ResultID returnType, std::vector<ResultID> parameterTypes, size_t pointerSize):
				backingType(BackingType::Function),
				functionReturnType(returnType),
				functionParameterTypes(parameterTypes),
				size(pointerSize),
				alignment(pointerSize)
				{};
			explicit Type(const PointerTag&, StorageClass storageClass, ResultID targetType, size_t pointerSize):
				backingType(BackingType::Pointer),
				pointerStorageClass(storageClass),
				pointerVectorMatrixArrayTargetType(targetType),
				size(pointerSize),
				alignment(pointerSize)
				{};
			explicit Type(const RuntimeArrayTag&, ResultID elementType, size_t elementSize, size_t arrayAlignment):
				backingType(BackingType::RuntimeArray),
				pointerVectorMatrixArrayTargetType(elementType),
				size(elementSize),
				alignment(arrayAlignment)
				{};

			bool operator==(const Type& other) const;

			bool operator!=(const Type& other) const {
				return !(*this == other);
			};
		};

		struct Type::Member {
			ResultID id;
			size_t offset;
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
		result = ((result << 1) ^ std::hash<size_t>()(type.scalarWidth)) >> 1;
		result = ((result << 1) ^ std::hash<bool>()(type.integerIsSigned)) >> 1;
		result = ((result << 1) ^ std::hash<size_t>()(type.vectorMatrixEntryCount)) >> 1;
		for (const auto& member: type.structureMembers) {
			result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(member.id)) >> 1;
			for (const auto& decoration: member.decorations) {
				result = ((result << 1) ^ std::hash<Iridium::SPIRV::Decoration>()(decoration)) >> 1;
			}
		}
		result = ((result << 1) ^ std::hash<uint32_t>()(static_cast<uint32_t>(type.pointerStorageClass))) >> 1;
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(type.pointerVectorMatrixArrayTargetType)) >> 1;
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(type.functionReturnType)) >> 1;
		for (const auto& param: type.functionParameterTypes) {
			result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(param)) >> 1;
		}
		return result;
	};
};

template<>
struct std::hash<std::pair<uintmax_t, Iridium::SPIRV::ResultID>> {
	size_t operator()(const std::pair<uintmax_t, Iridium::SPIRV::ResultID>& type) const {
		size_t result = std::hash<uintmax_t>()(type.first);
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(type.second)) >> 1;
		return result;
	};
};

namespace Iridium {
	namespace SPIRV {
		struct GlobalVariable {
			StorageClass storageClass;
			ResultID typeID;
		};

		struct FunctionVariable {
			ResultID typeID;
			ResultID initializer;
		};

		enum class Opcode: uint16_t {
			Nop = 0,
			Undef = 1,
			MemoryModel = 14,
			EntryPoint = 15,
			Capability = 17,
			TypeVoid = 19,
			TypeBool = 20,
			TypeInt = 21,
			TypeFloat = 22,
			TypeVector = 23,
			TypeMatrix = 24,
			TypeImage = 25,
			TypeSampler = 26,
			TypeSampledImage = 27,
			TypeArray = 28,
			TypeRuntimeArray = 29,
			TypeStruct = 30,
			TypeOpaque = 31,
			TypePointer = 32,
			TypeFunction = 33,
			ConstantTrue = 41,
			ConstantFalse = 42,
			Constant = 43,
			ConstantComposite = 44,
			ConstantSampler = 45,
			ConstantNull = 46,
			Function = 54,
			FunctionParameter = 55,
			FunctionEnd = 56,
			Variable = 59,
			Load = 61,
			Store = 62,
			AccessChain = 65,
			PtrAccessChain = 67,
			Decorate = 71,
			MemberDecorate = 72,
			VectorShuffle = 79,
			CompositeExtract = 81,
			CompositeInsert = 82,
			ConvertUToF = 112,
			UConvert = 113,
			FMul = 133,
			FDiv = 136,
			Label = 248,
			Return = 253,
			ReturnValue = 254,
		};

		struct FunctionInfo {
			ResultID id;
			std::vector<ResultID> parameterIDs;
		};

		class Builder {
		private:
			struct PrivateFunctionInfo {
				FunctionInfo idInfo;
				ResultID functionType;
				ResultID firstLabel;
				std::unordered_map<ResultID, FunctionVariable> variables;
			};

			DynamicByteWriter _writer;
			std::unordered_map<Type, ResultID> _typeIDs;
			std::unordered_map<ResultID, const Type*> _reverseTypeIDs;
			ResultID _currentResultID = 1;
			std::vector<EntryPoint> _entryPoints;
			std::unordered_map<uintptr_t, ResultID> _associatedIDs;
			std::unordered_map<ResultID, std::unordered_set<Decoration>> _decorations;
			std::unordered_map<ResultID, GlobalVariable> _globalVariables;
			AddressingModel _addressingModel;
			MemoryModel _memoryModel;
			DynamicByteWriter _constants;
			std::unordered_map<ResultID, PrivateFunctionInfo> _functionInfos;
			std::unordered_map<ResultID, DynamicByteWriter> _functionWriters;
			PrivateFunctionInfo* _currentFunctionInfo = nullptr;
			DynamicByteWriter* _currentFunctionWriter = nullptr;
			std::unordered_set<Capability> _requiredCapabilities;
			uint8_t _versionMajor = 1;
			uint8_t _versionMinor = 0;
			uint16_t _generatorID = 0;
			uint16_t _generatorVersion = 0;
			std::unordered_map<std::pair<uintmax_t, ResultID>, ResultID> _constantScalars;
			std::unordered_map<ResultID, ResultID> _undefinedValues;
			std::unordered_map<ResultID, ResultID> _resultIDTypes;

			struct InstructionState {
				size_t position;
				uint16_t opcode;
				DynamicByteWriter* writer;
			};

			void encodeInstructionHeader(DynamicByteWriter& writer, uint16_t wordCount, uint16_t opcode);
			void encodeString(std::string_view string);
			InstructionState beginInstruction(uint16_t opcode, DynamicByteWriter& writer);
			InstructionState beginInstruction(Opcode opcode, DynamicByteWriter& writer);
			void endInstruction(InstructionState&& state);

			ResultID declareConstantScalarCommon(uintmax_t value, ResultID typeID, bool usesTwoWords);

		public:
			Builder();

			ResultID declareType(const Type& type);
			ResultID lookupType(const Type& type) const;
			std::optional<Type> reverseLookupType(ResultID id) const;

			void setResultType(ResultID result, ResultID type);
			ResultID lookupResultType(ResultID result) const;

			void setAddressingModel(AddressingModel addressingModel);
			void setMemoryModel(MemoryModel memoryModel);
			void setVersion(uint8_t major, uint8_t minor);
			void setGeneratorID(uint16_t id, uint16_t version);

			void requireCapability(Capability capability);

			ResultID reserveResultID();
			ResultID associateResultID(uintptr_t association);
			void associateExistingResultID(ResultID id, uintptr_t association);
			ResultID lookupResultID(uintptr_t association) const;

			void addEntryPoint(const EntryPoint& entryPoint);
			ResultID addGlobalVariable(ResultID typeID, StorageClass storageClass);
			void addDecoration(ResultID resultID, const Decoration& decoration);
			FunctionInfo declareFunction(ResultID functionType);

			ResultID beginFunction(ResultID id);
			void endFunction();

			template<typename T>
			ResultID declareConstantScalar(T value);

			ResultID declareConstantComposite(ResultID typeID, std::vector<ResultID> elements);

			ResultID declareUndefinedValue(ResultID typeID);

			ResultID insertLabel();
			void referenceGlobalVariable(ResultID id);
			ResultID encodeUConvert(ResultID typeID, ResultID operand);
			ResultID encodeAccessChain(ResultID resultTypeID, ResultID base, std::vector<ResultID> indices, bool asPointer = false);
			ResultID encodeLoad(ResultID resultTypeID, ResultID pointer, uint8_t alignment = 0);
			void encodeStore(ResultID pointer, ResultID object);
			ResultID encodeConvertUToF(ResultID resultTypeID, ResultID operand);
			ResultID encodeFMul(ResultID resultTypeID, ResultID operand1, ResultID operand2);
			ResultID encodeFDiv(ResultID resultTypeID, ResultID operand1, ResultID operand2);
			ResultID encodeVectorShuffle(ResultID resultTypeID, ResultID vector1, ResultID vector2, std::vector<uint32_t> components);
			ResultID encodeCompositeInsert(ResultID resultTypeID, ResultID modifiedPart, ResultID compositeToCopyAndModify, std::vector<uint32_t> indices);
			ResultID encodeCompositeExtract(ResultID resultTypeID, ResultID composite, std::vector<uint32_t> indices);
			void encodeReturn(ResultID value = ResultIDInvalid);

			void* finalize(size_t& outputSize);
		};

		template<> ResultID Builder::declareConstantScalar<uint32_t>(uint32_t value);
		template<> ResultID Builder::declareConstantScalar<int32_t>(int32_t value);
		template<> ResultID Builder::declareConstantScalar<uint64_t>(uint64_t value);
		template<> ResultID Builder::declareConstantScalar<int64_t>(int64_t value);
		template<> ResultID Builder::declareConstantScalar<float>(float value);
		template<> ResultID Builder::declareConstantScalar<double>(double value);
	};
};
