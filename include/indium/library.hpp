#pragma once

#include <indium/base.hpp>
#include <indium/types.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Indium {
	class Device;
	class Library;

	/**
	 * A direct member of a structure a function takes in a buffer, as the shader declares it.
	 */
	struct ArgumentMemberInfo {
		size_t offset = 0;
		std::string typeName;
		std::string name;
	};

	enum class ArgumentKind {
		Buffer,
		Texture,
		Sampler,
		/** A vertex function's per-vertex input (a [[attribute(n)]] stage-in member); `index` is the attribute index. */
		VertexInput,
	};

	/**
	 * A buffer, texture, sampler or vertex input argument of a function, as the shader declares it. For
	 * buffers, textures and samplers `index` is the Metal argument table index; for a vertex input it is
	 * the attribute index. `size` and `alignment` describe a buffer's pointee type and are zero for the
	 * other kinds.
	 */
	struct ArgumentInfo {
		ArgumentKind kind = ArgumentKind::Buffer;
		size_t index = 0;
		std::string name;
		std::string typeName;
		size_t size = 0;
		size_t alignment = 0;
		std::vector<ArgumentMemberInfo> members;
	};

	/**
	 * The kind of resource a reflected function reads or writes.
	 */
	enum class BindingType: uint8_t {
		Buffer = 0,
		Texture = 1,
		Sampler = 2,
		VertexInput = 3,
	};

	/**
	 * How a function uses a texture binding. Only meaningful for
	 * BindingType::Texture; anything but Sample makes indium bind the texture
	 * as a storage image, so a wrong value here reads or writes a resource the
	 * shader declared as sampled.
	 */
	enum class TextureAccessType: uint8_t {
		Sample = 0,
		Read = 1,
		Write = 2,
		ReadWrite = 3,
	};

	/**
	 * One resource a reflected function accesses, in both of the index spaces
	 * indium needs.
	 */
	struct BindingDescriptor {
		/**
		 * The index the caller passes to the matching setter, i.e. the Metal
		 * argument index of the resource. For BindingType::Sampler it is
		 * `SIZE_MAX` when the function uses an embedded sampler, because the
		 * caller supplies no sampler for it.
		 */
		size_t index = 0;

		/**
		 * The Vulkan descriptor binding number this resource occupies in the
		 * descriptor set for the function's stage. Ignored for
		 * BindingType::Buffer, whose addresses all arrive through the single
		 * uniform buffer indium binds for the stage.
		 */
		size_t internalIndex = 0;

		/**
		 * Index into FunctionReflection::embeddedSamplers, or `SIZE_MAX` when
		 * this binding is not an embedded sampler.
		 */
		size_t embeddedSamplerIndex = std::numeric_limits<size_t>::max();

		BindingType type = BindingType::Buffer;
		TextureAccessType textureAccessType = TextureAccessType::Sample;

		/**
		 * Only meaningful for BindingType::Texture: the function declares a texture_buffer, which is bound as a
		 * texel buffer (uniform for Sample and Read, storage for Write and ReadWrite) and has to be a
		 * MTLTextureTypeTextureBuffer texture.
		 */
		bool texelBuffer = false;
	};

	/**
	 * The state of a sampler declared in the shader source rather than supplied
	 * by the caller. Mirrors the MSL `[[sampler(...)]]` attribute.
	 */
	struct EmbeddedSamplerDescriptor {
		enum class AddressMode: uint8_t {
			ClampToZero = 0,
			ClampToEdge = 1,
			Repeat = 2,
			MirrorRepeat = 3,
			ClampToBorderColor = 4,
		};

		enum class Filter: uint8_t {
			Nearest = 0,
			Linear = 1,
		};

		enum class MipFilter: uint8_t {
			None = 0,
			Nearest = 1,
			Linear = 2,
		};

		enum class CompareFunction: uint8_t {
			None = 0,
			Less = 1,
			LessEqual = 2,
			Greater = 3,
			GreaterEqual = 4,
			Equal = 5,
			NotEqual = 6,
			Always = 7,
			Never = 8,
		};

		enum class BorderColor: uint8_t {
			TransparentBlack = 0,
			OpaqueBlack = 1,
			OpaqueWhite = 2,
		};

		AddressMode widthAddressMode = AddressMode::ClampToEdge;
		AddressMode heightAddressMode = AddressMode::ClampToEdge;
		AddressMode depthAddressMode = AddressMode::ClampToEdge;
		Filter magnificationFilter = Filter::Nearest;
		Filter minificationFilter = Filter::Nearest;
		MipFilter mipmapFilter = MipFilter::None;
		bool usesNormalizedCoordinates = true;
		CompareFunction compareFunction = CompareFunction::None;
		uint8_t anisotropyLevel = 1;
		BorderColor borderColor = BorderColor::TransparentBlack;
		float lodMin = 0.0f;
		float lodMax = 0.0f;
	};

	/**
	 * The scalar types a function constant can have. Each is passed to the pipeline at its own size, a
	 * Bool as a VkBool32.
	 */
	enum class FunctionConstantType: uint8_t {
		Bool = 0,
		Int = 1,
		UInt = 2,
		Float = 3,
		Char = 4,
		UChar = 5,
		Short = 6,
		UShort = 7,
		Half = 8,
		Long = 9,
		ULong = 10,
	};

	/**
	 * A function constant ([[function_constant(n)]]) of a function, as the module declares it.
	 * The module declares two specialization constants for it, and indium sets them when a pipeline
	 * is created from a function made with values.
	 */
	struct FunctionConstantInfo {
		std::string name;
		FunctionConstantType type = FunctionConstantType::Int;

		/** The Metal function constant index. */
		size_t index = 0;

		/**
		 * The SpecId of the constant's value: a scalar of `type` (a VkBool32 for Bool).
		 */
		uint32_t specializationID = 0;

		/**
		 * The SpecId of a VkBool32 that must be true for the module to treat the constant as defined
		 * (is_function_constant_defined); indium sets it for every constant that was given a value.
		 */
		uint32_t definedSpecializationID = 0;
	};

	/**
	 * Values for the function constants of a function, by index or by name.
	 */
	class FunctionConstantValues {
	public:
		struct Value {
			FunctionConstantType type = FunctionConstantType::Int;

			/** The value: 0 or 1 for Bool, otherwise the bit pattern of the type in the low bits. */
			uint64_t bits = 0;
		};

		void setValue(size_t index, FunctionConstantType type, uint64_t bits) { _byIndex[index] = Value { type, bits }; }
		void setValue(const std::string& name, FunctionConstantType type, uint64_t bits) { _byName[name] = Value { type, bits }; }
		void reset() { _byIndex.clear(); _byName.clear(); }

		const std::map<size_t, Value>& byIndex() const { return _byIndex; }
		const std::map<std::string, Value>& byName() const { return _byName; }

	private:
		std::map<size_t, Value> _byIndex;
		std::map<std::string, Value> _byName;
	};

	/**
	 * One varying that crosses from the vertex stage to the fragment stage: a field of a vertex function's
	 * returned struct, or of a fragment function's stage-in struct.
	 */
	struct StageInterfaceField {
		/** The name the two stages pair it by: the [[user(name)]] of the field, or the field's own name. */
		std::string name;

		/** The shader type of the field, as the producer spells it (the pairing needs equal types). */
		std::string type;

		/** The Location of the variable in the module. */
		uint32_t location = 0;

		bool operator==(const StageInterfaceField& other) const {
			return name == other.name && type == other.type && location == other.location;
		}
	};

	/**
	 * A kernel argument in threadgroup memory ([[threadgroup(n)]]), whose length in bytes the host sets with
	 * ComputeCommandEncoder::setThreadgroupMemoryLength(). The module declares it as a Workgroup array whose
	 * length, in elements, is a specialization constant; indium sets that constant when it creates the pipeline
	 * for a dispatch, to the number of whole elements in the length set for `index`, and to at least 1.
	 */
	struct ThreadgroupArgumentInfo {
		/** The Metal threadgroup memory index, the one setThreadgroupMemoryLength() takes. */
		size_t index = 0;

		/** The SpecId of the array's length: a 32-bit unsigned integer. */
		uint32_t specializationID = 0;

		/** The size of one element of the array in bytes, as Metal lays it out. */
		size_t elementSize = 0;
	};

	/**
	 * What a producer knows about one function of a module it has already
	 * compiled.
	 */
	struct FunctionReflection {
		/**
		 * The stage this function is entered at. Must be Vertex, Fragment or
		 * Kernel: indium derives the descriptor set layout, the shader stage
		 * flags and the compute pipeline from it.
		 */
		FunctionType functionType = FunctionType::Invalid;

		/**
		 * Every resource the function accesses, in either order. indium builds
		 * the descriptor set layout for the function's stage from this list and
		 * from nothing else, so a resource the producer omits here is one the
		 * shader cannot reach no matter what the caller binds.
		 */
		std::vector<BindingDescriptor> bindings;

		/**
		 * The samplers the function carries with it. `bindings` entries with an
		 * `index` of `SIZE_MAX` and a `type` of Sampler refer into this list.
		 */
		std::vector<EmbeddedSamplerDescriptor> embeddedSamplers;

		/**
		 * For a vertex function, the varyings it returns and, for a fragment function, the ones it reads,
		 * each at the Location it has in the module. A vertex output and a fragment input are paired by
		 * name and type when a pipeline is made, whatever their Locations, so a fragment function that
		 * has Locations of a different numbering than its vertex function's still reads the right
		 * varyings. A function with none listed (a producer that numbers them consistently) is left as it is.
		 */
		std::vector<StageInterfaceField> stageInterface;

		/**
		 * The function constants the module declares for this function. Their SpecIds must be distinct
		 * and, for a kernel, not 0 to 2, which carry its threadgroup size.
		 */
		std::vector<FunctionConstantInfo> functionConstants;

		/**
		 * For a kernel only: the specialization constants, one per dimension, that hold the grid size the kernel
		 * keeps its threads inside. A kernel that has them skips every thread whose position in the grid is
		 * beyond them, and they default to a value that keeps every thread, so a dispatch of whole
		 * threadgroups sets none. A dispatch of a grid that is not a multiple of the threadgroup size
		 * (Metal's dispatchThreads:) runs whole threadgroups and sets them to the grid size; a kernel without
		 * them cannot be dispatched that way. The ids must be distinct from each other and from the ids of the
		 * function constants, and not 0 to 2.
		 */
		std::optional<std::array<uint32_t, 3>> gridSizeSpecializationIDs;

		/**
		 * For a kernel only: its threadgroup memory arguments. Their indices must be distinct, and their
		 * SpecIds distinct from each other and from the ids above, and not 0 to 2.
		 */
		std::vector<ThreadgroupArgumentInfo> threadgroupArguments;

		/**
		 * For a kernel only: the most threads a threadgroup of it may have, as its source declares it (MSL's
		 * [[max_total_threads_per_threadgroup(N)]]), or 0 when it declares none. A compute pipeline of the
		 * function allows no more than this.
		 */
		size_t maxTotalThreadsPerThreadgroup = 0;
	};

	/**
	 * The reflection indium needs to run a module some other producer compiled.
	 *
	 * This is a description of a module, not a serialization format. A producer
	 * that emits a document, a text section or a binary blob is free to do so;
	 * reading that document is the caller's job, and the caller hands indium
	 * these values. indium therefore has no opinion about the wire format, and
	 * no dependency on whatever the producer uses to express it.
	 *
	 * A module usually has one entry point, in which case this has one entry.
	 */
	struct LibraryReflection {
		/**
		 * Per-function reflection, keyed by the name the function is entered
		 * with. The key must be the entry point name as it appears in the
		 * module, because that is the name indium hands to
		 * `vkCreateComputePipelines`/`vkCreateGraphicsPipelines` as `pName`.
		 */
		std::unordered_map<std::string, FunctionReflection> functions;
	};

	class Function {
	public:
		virtual ~Function() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		virtual FunctionType functionType() const = 0;

		// the buffer, texture and sampler arguments the function declares, for pipeline reflection
		virtual const std::vector<ArgumentInfo>& argumentInfo() const = 0;

		/** The function constants the function declares, in no particular order. */
		virtual const std::vector<FunctionConstantInfo>& functionConstants() const = 0;

		INDIUM_PROPERTY_READONLY(std::string, n, N,ame);
	};

	class Library {
	public:
		virtual ~Library() = 0;

		virtual std::shared_ptr<Function> newFunction(const std::string& name) = 0;

		/**
		 * A function specialized with `values`: pipelines made from it run the shader with those function
		 * constants. A constant without a value is undefined, which a shader may test for.
		 *
		 * Values for constants the function does not declare are ignored, so one set of values can serve
		 * several functions.
		 *
		 * @returns nullptr if the library has no such function, or, with `errorMessage` set, if a value
		 *          has another type than the constant it names.
		 */
		virtual std::shared_ptr<Function> newFunction(const std::string& name, const FunctionConstantValues& values, std::string* errorMessage = nullptr) = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
