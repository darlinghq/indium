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
			FragCoord = 15,
			PointCoord = 16,
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

		enum class Dim: uint32_t {
			e1D = 0,
			e2D,
			e3D,
			eCube,
			eRect,
			eBuffer,
			eSubpassData,
		};

		enum class ImageFormat: uint32_t {
			Unknown = 0,
			Rgba32f,
			Rgba16f,
			R32f,
			Rgba8,
			Rgba8Snorm,
			Rg32f,
			Rg16f,
			R11fG11fB10f,
			R16f,
			Rgba16,
			Rgb10A2,
			Rg16,
			Rg8,
			R16,
			R8,
			Rgba16Snorm,
			Rg16Snorm,
			Rg8Snorm,
			R16Snorm,
			R8Snorm,
			Rgba32i,
			Rgba16i,
			Rgba8i,
			R32i,
			Rg32i,
			Rg16i,
			Rg8i,
			R16i,
			R8i,
			Rgba32ui,
			Rgba16ui,
			Rgba8ui,
			R32ui,
			Rgb10a2ui,
			Rg32ui,
			Rg16ui,
			Rg8ui,
			R16ui,
			R8ui,
			R64ui,
			R64i,
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
				Image,
				Sampler,
				SampledImage,
				Array,
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
			struct ImageTag {};
			struct SamplerTag {};
			struct SampledImageTag {};
			struct ArrayTag {};

			struct Member;

			BackingType backingType = BackingType::Structure;

			size_t scalarWidth = 0;

			bool integerIsSigned = false;

			// used by vector, matrix, array
			size_t entryCount = 0;
			size_t size = 0;
			size_t alignment = 1;

			std::vector<Member> structureMembers;

			StorageClass pointerStorageClass = StorageClass::UniformConstant;

			// used by pointer, vector, matrix, array, image, and sampled image
			ResultID targetType = ResultIDInvalid;

			ResultID functionReturnType = ResultIDInvalid;
			std::vector<ResultID> functionParameterTypes;

			Dim imageDimensionality = Dim::e1D;
			uint8_t imageDepthIndication = 2;
			bool imageIsArrayed = false;
			bool imageIsMultisampled = false;
			uint8_t imageIsSampledIndication = 0;
			ImageFormat imageFormat = ImageFormat::Unknown;
			ResultID imageRealSampleTypeID = ResultIDInvalid;

			// this does NOT need to be considered for equality
			ResultID arrayLengthID = ResultIDInvalid;

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
				entryCount(componentCount),
				targetType(componentType),
				size(totalSize),
				alignment(vectorAlignment)
				{};
			explicit Type(const MatrixTag&, size_t columnCount, ResultID entryType, size_t totalSize, size_t matrixAlignment):
				backingType(BackingType::Matrix),
				entryCount(columnCount),
				targetType(entryType),
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
				targetType(targetType),
				size(pointerSize),
				alignment(pointerSize)
				{};
			explicit Type(const RuntimeArrayTag&, ResultID elementType, size_t elementSize, size_t arrayAlignment):
				backingType(BackingType::RuntimeArray),
				targetType(elementType),
				size(0),
				alignment(arrayAlignment)
				{};
			explicit Type(const ImageTag&, ResultID fakeSampleType, ResultID realSampleType, Dim dimensionality, uint8_t depthIndication, bool isArrayed, bool isMultisampled, uint8_t isSampledIndication, ImageFormat format):
				backingType(BackingType::Image),
				targetType(fakeSampleType),
				imageDimensionality(dimensionality),
				imageDepthIndication(depthIndication),
				imageIsArrayed(isArrayed),
				imageIsMultisampled(isMultisampled),
				imageIsSampledIndication(isSampledIndication),
				imageFormat(format),
				imageRealSampleTypeID(realSampleType)
				{};
			explicit Type(const SamplerTag&):
				backingType(BackingType::Sampler)
				{};
			explicit Type(const SampledImageTag&, ResultID sampledImageType):
				backingType(BackingType::SampledImage),
				targetType(sampledImageType)
				{};
			explicit Type(const ArrayTag&, ResultID elementType, size_t elementCount, size_t totalSize, size_t arrayAlignment):
				backingType(BackingType::Array),
				targetType(elementType),
				entryCount(elementCount),
				size(totalSize),
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
		result = ((result << 1) ^ std::hash<size_t>()(type.entryCount)) >> 1;
		for (const auto& member: type.structureMembers) {
			result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(member.id)) >> 1;
			for (const auto& decoration: member.decorations) {
				result = ((result << 1) ^ std::hash<Iridium::SPIRV::Decoration>()(decoration)) >> 1;
			}
		}
		result = ((result << 1) ^ std::hash<uint32_t>()(static_cast<uint32_t>(type.pointerStorageClass))) >> 1;
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(type.targetType)) >> 1;
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(type.functionReturnType)) >> 1;
		for (const auto& param: type.functionParameterTypes) {
			result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(param)) >> 1;
		}
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::Dim>()(type.imageDimensionality)) >> 1;
		result = ((result << 1) ^ std::hash<uint8_t>()(type.imageDepthIndication)) >> 1;
		result = ((result << 1) ^ std::hash<bool>()(type.imageIsArrayed)) >> 1;
		result = ((result << 1) ^ std::hash<bool>()(type.imageIsMultisampled)) >> 1;
		result = ((result << 1) ^ std::hash<uint8_t>()(type.imageIsSampledIndication)) >> 1;
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::ImageFormat>()(type.imageFormat)) >> 1;
		result = ((result << 1) ^ std::hash<Iridium::SPIRV::ResultID>()(type.imageRealSampleTypeID)) >> 1;
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

		enum class ExecutionMode: uint32_t {
			OriginUpperLeft = 7,
			OriginLowerLeft = 8,
		};

		enum class Opcode: uint16_t {
			Nop = 0,
			Undef = 1,
			String = 7,
			Extension = 10,
			ExtInstImport = 11,
			ExtInst = 12,
			MemoryModel = 14,
			EntryPoint = 15,
			ExecutionMode = 16,
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
			VectorExtractDynamic = 77,
			VectorInsertDynamic = 78,
			VectorShuffle = 79,
			CompositeExtract = 81,
			CompositeInsert = 82,
			SampledImage = 86,
			ImageSampleImplicitLod = 87,
			ConvertUToF = 112,
			UConvert = 113,
			FConvert = 115,
			QuantizeToF16 = 116,
			ConvertPtrToU = 117,
			ConvertUToPtr = 120,
			Bitcast = 124,
			SNegate = 126,
			FNegate = 127,
			IAdd = 128,
			FAdd = 129,
			ISub = 130,
			FSub = 131,
			IMul = 132,
			FMul = 133,
			UDiv = 134,
			SDiv = 133,
			FDiv = 136,
			UMod = 137,
			SRem = 138,
			SMod = 139,
			FRem = 140,
			FMod = 141,
			Dot = 148,
			FOrdLessThan = 184,
			FOrdGreaterThan = 186,
			Phi = 245,
			SelectionMerge = 247,
			Label = 248,
			Branch = 249,
			BranchConditional = 250,
			Return = 253,
			ReturnValue = 254,
		};

		enum class GLSLOpcode: uint16_t {
			Round = 1,
			RoundEven = 2,
			Trunc = 3,
			FAbs = 4,
			SAbs = 5,
			FSign = 6,
			SSign = 7,
			Floor = 8,
			Ceil = 9,
			Fract = 10,
			Radians = 11,
			Degrees = 12,
			Sin = 13,
			Cos = 14,
			Tan = 15,
			Asin = 16,
			Acos = 17,
			Atan = 18,
			Sinh = 19,
			Cosh = 20,
			Tanh = 21,
			Asinh = 22,
			Acosh = 23,
			Atanh = 24,
			Atan2 = 25,
			Pow = 26,
			Exp = 27,
			Log = 28,
			Exp2 = 29,
			Log2 = 30,
			Sqrt = 31,
			InverseSqrt = 32,
			Determinant = 33,
			MatrixInverse = 34,
			Modf = 35,
			ModfStruct = 36,
			FMin = 37,
			UMin = 38,
			SMin = 39,
			FMax = 40,
			UMax = 41,
			SMax = 42,
			FClamp = 43,
			UClamp = 44,
			SClamp = 45,
			FMix = 46,
			Step = 48,
			SmoothStep = 49,
			Fma = 50,
			Frexp = 51,
			FrexpStruct = 52,
			Ldexp = 53,
			PackSnorm4x8 = 54,
			PackUnorm4x8 = 55,
			PackSnorm2x16 = 56,
			PackUnorm2x16 = 57,
			PackHalf2x16 = 58,
			PackDouble2x32 = 59,
			UnpackSnorm2x16 = 60,
			UnpackUnorm2x16 = 61,
			UnpackHalf2x16 = 62,
			UnpackSnorm4x8 = 63,
			UnpackUnorm4x8 = 64,
			UnpackDouble2x32 = 65,
			Length = 66,
			Distance = 67,
			Cross = 68,
			Normalize = 69,
			FaceForward = 70,
			Reflect = 71,
			Refract = 72,
			FindLsb = 73,
			FindSMsb = 74,
			FindUMsb = 75,
			InterpolateAtCentroid = 76,
			InterpolateAtSample = 77,
			InterpolateAtOffset = 78,
			NMin = 79,
			NMax = 80,
			NClamp = 81,
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
				std::vector<ResultID> referencedGlobalVariables;
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
			std::unordered_map<ResultID, ResultID> _nullValues;
			std::unordered_map<ResultID, ResultID> _resultIDTypes;
			std::unordered_set<std::string> _extensions;
			ResultID _debugPrintf = ResultIDInvalid;
			std::vector<std::pair<ResultID, std::string>> _strings;
			ResultID _glslExtInstSet = ResultIDInvalid;

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

			void ensureGLSLExtInstSet();

			InstructionState beginGLSLInstruction(GLSLOpcode opcode, DynamicByteWriter& writer, ResultID resultTypeID, ResultID resultID);

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
			ResultID declareNullValue(ResultID typeID);

			ResultID insertLabel(ResultID id = ResultIDInvalid);
			void referenceGlobalVariable(ResultID id);
			ResultID encodeUConvert(ResultID typeID, ResultID operand);
			ResultID encodeAccessChain(ResultID resultTypeID, ResultID base, std::vector<ResultID> indices, bool asPointer = false);
			ResultID encodeLoad(ResultID resultTypeID, ResultID pointer, uint8_t alignment = 0);
			void encodeStore(ResultID pointer, ResultID object);
			ResultID encodeConvertUToF(ResultID resultTypeID, ResultID operand);
			ResultID encodeArithUnop(Opcode unop, ResultID resultTypeID, ResultID operand);
			ResultID encodeArithBinop(Opcode binop, ResultID resultTypeID, ResultID operand1, ResultID operand2);
			ResultID encodeVectorShuffle(ResultID resultTypeID, ResultID vector1, ResultID vector2, std::vector<uint32_t> components);
			ResultID encodeCompositeInsert(ResultID resultTypeID, ResultID modifiedPart, ResultID compositeToCopyAndModify, std::vector<uint32_t> indices);
			ResultID encodeCompositeExtract(ResultID resultTypeID, ResultID composite, std::vector<uint32_t> indices);
			void encodeReturn(ResultID value = ResultIDInvalid);
			ResultID encodeConvertPtrToU(ResultID resultTypeID, ResultID target);
			ResultID encodeConvertUToPtr(ResultID resultTypeID, ResultID target);
			ResultID encodeBitcast(ResultID resultTypeID, ResultID operand);
			ResultID encodeSampledImage(ResultID resultTypeID, ResultID image, ResultID sampler);
			ResultID encodeImageSampleImplicitLod(ResultID resultTypeID, ResultID sampledImage, ResultID coordinates);
			ResultID encodeFConvert(ResultID resultTypeID, ResultID target);
			ResultID encodeInverseSqrt(ResultID resultTypeID, ResultID target);
			ResultID encodeFClamp(ResultID resultTypeID, ResultID target, ResultID min, ResultID max);
			ResultID encodeFMax(ResultID resultTypeID, ResultID operand1, ResultID operand2);
			ResultID encodePow(ResultID resultTypeID, ResultID base, ResultID exponent);
			ResultID encodeVectorExtractDynamic(ResultID resultTypeID, ResultID vector, ResultID index);
			ResultID encodeVectorInsertDynamic(ResultID resultTypeID, ResultID vector, ResultID component, ResultID index);
			ResultID encodeFOrdLessThan(ResultID operand1, ResultID operand2);
			ResultID encodeFOrdGreaterThan(ResultID operand1, ResultID operand2);
			void encodeBranch(ResultID targetLabel);
			void encodeBranchConditional(ResultID condition, ResultID trueLabel, ResultID falseLabel);
			ResultID encodePhi(ResultID resultTypeID, std::vector<std::pair<ResultID, ResultID>> variablesAndBlocks);
			void encodeSelectionMerge(ResultID mergeBlock);
			ResultID encodeSqrt(ResultID resultTypeID, ResultID target);
			ResultID encodeTrunc(ResultID resultTypeID, ResultID operand);

			void encodeDebugPrint(std::string format, std::vector<ResultID> arguments);

			void* finalize(size_t& outputSize);
		};

		template<> ResultID Builder::declareConstantScalar<uint8_t>(uint8_t value);
		template<> ResultID Builder::declareConstantScalar<int8_t>(int8_t value);
		template<> ResultID Builder::declareConstantScalar<uint16_t>(uint16_t value);
		template<> ResultID Builder::declareConstantScalar<int16_t>(int16_t value);
		template<> ResultID Builder::declareConstantScalar<uint32_t>(uint32_t value);
		template<> ResultID Builder::declareConstantScalar<int32_t>(int32_t value);
		template<> ResultID Builder::declareConstantScalar<uint64_t>(uint64_t value);
		template<> ResultID Builder::declareConstantScalar<int64_t>(int64_t value);
		template<> ResultID Builder::declareConstantScalar<_Float16>(_Float16 value);
		template<> ResultID Builder::declareConstantScalar<float>(float value);
		template<> ResultID Builder::declareConstantScalar<double>(double value);
	};
};
