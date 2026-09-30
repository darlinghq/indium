#pragma once

#include <indium/base.hpp>
#include <indium/types.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
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
	};

	/**
	 * A buffer, texture or sampler argument of a function, as the shader declares it. `index` is the
	 * Metal argument table index; `size` and `alignment` describe a buffer's pointee type and are zero
	 * for textures and samplers.
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

		INDIUM_PROPERTY_READONLY(std::string, n, N,ame);
	};

	class Library {
	public:
		virtual ~Library() = 0;

		virtual std::shared_ptr<Function> newFunction(const std::string& name) = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
