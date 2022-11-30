#pragma once

#include <cstddef>

#include <vector>
#include <unordered_map>
#include <string>

namespace Iridium {
	void init();
	void finit();

	enum class FunctionType {
		Vertex = 1,
		Fragment = 2,
	};

	enum class BindingType {
		Buffer,
		Texture,
		Sampler,
		VertexInput,
	};

	enum class TextureAccessType {
		Sample,
		Read,
		Write,
		ReadWrite,
	};

	struct BindingInfo {
		BindingType type;
		size_t index;
		size_t internalIndex;
		TextureAccessType textureAccessType;
		size_t embeddedSamplerIndex;
	};

	struct EmbeddedSampler {
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

		AddressMode widthAddressMode;
		AddressMode heightAddressMode;
		AddressMode depthAddressMode;
		Filter magnificationFilter;
		Filter minificationFilter;
		MipFilter mipmapFilter;
		bool usesNormalizedCoordinates;
		CompareFunction compareFunction;
		uint8_t anisotropyLevel;
		BorderColor borderColor;
		float lodMin;
		float lodMax;
	};

	struct FunctionInfo {
		FunctionType type;
		std::vector<BindingInfo> bindings;
		std::vector<EmbeddedSampler> embeddedSamplers;
	};

	struct OutputInfo {
		std::unordered_map<std::string, FunctionInfo> functionInfos;
	};

	/**
	 * @returns A pointer allocated with `malloc`, or `nullptr` if translation failed.
	 */
	void* translate(const void* inputData, size_t inputSize, size_t& outputSize, OutputInfo& outputInfo);
};
