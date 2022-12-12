#pragma once

#include <cstddef>

#include <indium/base.hpp>

namespace Indium {
	template<typename T>
	struct Range {
		T start;
		T length;
	};

	struct SamplePosition {
		SamplePosition() {};
		SamplePosition(float _x, float _y):
			x(_x),
			y(_y)
			{};

		float x;
		float y;
	};

	enum class TextureSwizzle: size_t {
		Zero = 0,
		One = 1,
		Red = 2,
		Green = 3,
		Blue = 4,
		Alpha = 5,
	};

	struct TextureSwizzleChannels {
		TextureSwizzle red = TextureSwizzle::Red;
		TextureSwizzle green = TextureSwizzle::Green;
		TextureSwizzle blue = TextureSwizzle::Blue;
		TextureSwizzle alpha = TextureSwizzle::Alpha;
	};

	enum class PixelFormat: size_t {
		Invalid = 0,
		A8Unorm = 1,
		R8Unorm = 10,
		R8Unorm_sRGB = 11,
		R8Snorm = 12,
		R8Uint = 13,
		R8Sint = 14,
		R16Unorm = 20,
		R16Snorm = 22,
		R16Uint = 23,
		R16Sint = 24,
		R16Float = 25,
		RG8Unorm = 30,
		RG8Unorm_sRGB = 31,
		RG8Snorm = 32,
		RG8Uint = 33,
		RG8Sint = 34,
		B5G6R5Unorm = 40,
		A1BGR5Unorm = 41,
		ABGR4Unorm = 42,
		BGR5A1Unorm = 43,
		R32Uint = 53,
		R32Sint = 54,
		R32Float = 55,
		RG16Unorm = 60,
		RG16Snorm = 62,
		RG16Uint = 63,
		RG16Sint = 64,
		RG16Float = 65,
		RGBA8Unorm = 70,
		RGBA8Unorm_sRGB = 71,
		RGBA8Snorm = 72,
		RGBA8Uint = 73,
		RGBA8Sint = 74,
		BGRA8Unorm = 80,
		BGRA8Unorm_sRGB = 81,
		RGB10A2Unorm = 90,
		RGB10A2Uint = 91,
		RG11B10Float = 92,
		RGB9E5Float = 93,
		BGR10A2Unorm = 94,
		RG32Uint = 103,
		RG32Sint = 104,
		RG32Float = 105,
		RGBA16Unorm = 110,
		RGBA16Snorm = 112,
		RGBA16Uint = 113,
		RGBA16Sint = 114,
		RGBA16Float = 115,
		RGBA32Uint = 123,
		RGBA32Sint = 124,
		RGBA32Float = 125,
		BC1_RGBA = 130,
		BC1_RGBA_sRGB = 131,
		BC2_RGBA = 132,
		BC2_RGBA_sRGB = 133,
		BC3_RGBA = 134,
		BC3_RGBA_sRGB = 135,
		BC4_RUnorm = 140,
		BC4_RSnorm = 141,
		BC5_RGUnorm = 142,
		BC5_RGSnorm = 143,
		BC6H_RGBFloat = 150,
		BC6H_RGBUfloat = 151,
		BC7_RGBAUnorm = 152,
		BC7_RGBAUnorm_sRGB = 153,
		PVRTC_RGB_2BPP = 160,
		PVRTC_RGB_2BPP_sRGB = 161,
		PVRTC_RGB_4BPP = 162,
		PVRTC_RGB_4BPP_sRGB = 163,
		PVRTC_RGBA_2BPP = 164,
		PVRTC_RGBA_2BPP_sRGB = 165,
		PVRTC_RGBA_4BPP = 166,
		PVRTC_RGBA_4BPP_sRGB = 167,
		EAC_R11Unorm = 170,
		EAC_R11Snorm = 172,
		EAC_RG11Unorm = 174,
		EAC_RG11Snorm = 176,
		EAC_RGBA8 = 178,
		EAC_RGBA8_sRGB = 179,
		ETC2_RGB8 = 180,
		ETC2_RGB8_sRGB = 181,
		ETC2_RGB8A1 = 182,
		ETC2_RGB8A1_sRGB = 183,
		ASTC_4x4_sRGB = 186,
		ASTC_5x4_sRGB = 187,
		ASTC_5x5_sRGB = 188,
		ASTC_6x5_sRGB = 189,
		ASTC_6x6_sRGB = 190,
		ASTC_8x5_sRGB = 192,
		ASTC_8x6_sRGB = 193,
		ASTC_8x8_sRGB = 194,
		ASTC_10x5_sRGB = 195,
		ASTC_10x6_sRGB = 196,
		ASTC_10x8_sRGB = 197,
		ASTC_10x10_sRGB = 198,
		ASTC_12x10_sRGB = 199,
		ASTC_12x12_sRGB = 200,
		ASTC_4x4_LDR = 204,
		ASTC_5x4_LDR = 205,
		ASTC_5x5_LDR = 206,
		ASTC_6x5_LDR = 207,
		ASTC_6x6_LDR = 208,
		ASTC_8x5_LDR = 210,
		ASTC_8x6_LDR = 211,
		ASTC_8x8_LDR = 212,
		ASTC_10x5_LDR = 213,
		ASTC_10x6_LDR = 214,
		ASTC_10x8_LDR = 215,
		ASTC_10x10_LDR = 216,
		ASTC_12x10_LDR = 217,
		ASTC_12x12_LDR = 218,
		ASTC_4x4_HDR = 222,
		ASTC_5x4_HDR = 223,
		ASTC_5x5_HDR = 224,
		ASTC_6x5_HDR = 225,
		ASTC_6x6_HDR = 226,
		ASTC_8x5_HDR = 228,
		ASTC_8x6_HDR = 229,
		ASTC_8x8_HDR = 230,
		ASTC_10x5_HDR = 231,
		ASTC_10x6_HDR = 232,
		ASTC_10x8_HDR = 233,
		ASTC_10x10_HDR = 234,
		ASTC_12x10_HDR = 235,
		ASTC_12x12_HDR = 236,
		GBGR422 = 240,
		BGRG422 = 241,
		Depth16Unorm = 250,
		Depth32Float = 252,
		Stencil8 = 253,
		Depth24Unorm_Stencil8 = 255,
		Depth32Float_Stencil8 = 260,
		X32_Stencil8 = 261,
		X24_Stencil8 = 262,
		BGRA10_XR = 552,
		BGRA10_XR_sRGB = 553,
		BGR10_XR = 554,
		BGR10_XR_sRGB = 555,
	};

	enum class ColorWriteMask: size_t {
		None = 0,
		Alpha = 1,
		Blue = 2,
		Green = 4,
		Red = 8,
		All = 15,
	};
	INDIUM_BITFLAG_ENUM_CLASS(ColorWriteMask);

	enum class BlendOperation: size_t {
		Add = 0,
		Subtract = 1,
		ReverseSubtract = 2,
		Min = 3,
		Max = 4,
	};

	enum class BlendFactor: size_t {
		Zero = 0,
		One = 1,
		SourceColor = 2,
		OneMinusSourceColor = 3,
		SourceAlpha = 4,
		OneMinusSourceAlpha = 5,
		DestinationColor = 6,
		OneMinusDestinationColor = 7,
		DestinationAlpha = 8,
		OneMinusDestinationAlpha = 9,
		SourceAlphaSaturated = 10,
		BlendColor = 11,
		OneMinusBlendColor = 12,
		BlendAlpha = 13,
		OneMinusBlendAlpha = 14,
		Source1Color = 15,
		OneMinusSource1Color = 16,
		Source1Alpha = 17,
		OneMinusSource1Alpha = 18,
	};

	enum class LoadAction: size_t {
		DontCare = 0,
		Load = 1,
		Clear = 2,
		Default = 0xff,
	};

	enum class StoreAction: size_t {
		DontCare = 0,
		Store = 1,
		MultisampleResolve = 2,
		StoreAndMultisampleResolve = 3,
		Unknown = 4,
		CustomSampleDepthStore = 5,
		Default = 0xff,
	};

	enum class StoreActionOptions: size_t {
		None = 0,
		ValidMask = 1,
		CustomSamplePositions = 1,
	};

	enum class MultisampleDepthResolveFilter: size_t {
		Sample0 = 0,
		Min = 1,
		Max = 2,
	};

	enum class MultisampleStencilResolveFilter: size_t {
		Sample0 = 0,
		DepthResolvedSample = 1,
	};

	// `e` prefix required for members like `1D` and `2D`
	enum class TextureType {
		e1D = 0,
		e1DArray = 1,
		e2D = 2,
		e2DArray = 3,
		e2DMultisample = 4,
		eCube = 5,
		eCubeArray = 6,
		e3D = 7,
		e2DMultisampleArray = 8,
		eTextureBuffer = 9,
	};

	enum class TextureUsage {
		Unknown = 0,
		ShaderRead = 1 << 0,
		ShaderWrite = 1 << 1,
		RenderTarget = 1 << 2,
		PixelFormatView = 1 << 4,
	};
	INDIUM_BITFLAG_ENUM_CLASS(TextureUsage);

	enum class FunctionType: size_t {
		Vertex = 1,
		Fragment = 2,
		Kernel = 3,
		Visible = 5,
		Intersection = 6,
		Mesh = 7,
		Object = 8,
	};

	enum class PatchType: size_t {
		None = 0,
		Triangle = 1,
		Quad = 2,
	};

	enum class VertexFormat: size_t {
		Invalid = 0,
		UChar2 = 1,
		UChar3 = 2,
		UChar4 = 3,
		Char2 = 4,
		Char3 = 5,
		Char4 = 6,
		UChar2Normalized = 7,
		UChar3Normalized = 8,
		UChar4Normalized = 9,
		Char2Normalized = 10,
		Char3Normalized = 11,
		Char4Normalized = 12,
		UShort2 = 13,
		UShort3 = 14,
		UShort4 = 15,
		Short2 = 16,
		Short3 = 17,
		Short4 = 18,
		UShort2Normalized = 19,
		UShort3Normalized = 20,
		UShort4Normalized = 21,
		Short2Normalized = 22,
		Short3Normalized = 23,
		Short4Normalized = 24,
		Half2 = 25,
		Half3 = 26,
		Half4 = 27,
		Float = 28,
		Float2 = 29,
		Float3 = 30,
		Float4 = 31,
		Int = 32,
		Int2 = 33,
		Int3 = 34,
		Int4 = 35,
		UInt = 36,
		UInt2 = 37,
		UInt3 = 38,
		UInt4 = 39,
		Int1010102Normalized = 40,
		UInt1010102Normalized = 41,
		UChar4Normalized_BGRA = 42,
		UChar = 45,
		Char = 46,
		UCharNormalized = 47,
		CharNormalized = 48,
		UShort = 49,
		Short = 50,
		UShortNormalized = 51,
		ShortNormalized = 52,
		Half = 53,
	};

	enum class VertexStepFunction: size_t {
		Constant = 0,
		PerVertex = 1,
		PerInstance = 2,
		PerPatch = 3,
		PerPatchControlPoint = 4,
	};

	enum class Mutability: size_t {
		Default = 0,
		Mutable = 1,
		Immutable = 2,
	};

	enum class PrimitiveTopologyClass: size_t {
		Unspecified = 0,
		Point = 1,
		Line = 2,
		Triangle = 3,
	};

	enum class TessellationPartitionMode: size_t {
		Pow2 = 0,
		Integer = 1,
		FractionalOdd = 2,
		FractionalEven = 3,
	};

	enum class TessellationFactorStepFunction: size_t {
		Constant = 0,
		PerPatch = 1,
		PerInstance = 2,
		PerPatchAndPerInstance = 3,
	};

	enum class TessellationFactorFormat: size_t {
		Half = 0,
	};

	enum class TessellationControlPointIndexType: size_t {
		None = 0,
		UInt16 = 1,
		UInt32 = 2,
	};

	enum class CullMode: size_t {
		None = 0,
		Front = 1,
		Back = 2,
	};
	INDIUM_BITFLAG_ENUM_CLASS(CullMode);

	enum class Winding: size_t {
		Clockwise = 0,
		CounterClockwise = 1,
	};

	enum class DepthClipMode: size_t {
		Clip = 0,
		Clamp = 1,
	};

	enum class TriangleFillMode: size_t {
		Fill = 0,
		Lines = 1,
	};

	enum class PrimitiveType: size_t {
		Point = 0,
		Line = 1,
		LineStrip = 2,
		Triangle = 3,
		TriangleStrip = 4,

		LAST,
	};

	struct Viewport {
		double originX;
		double originY;
		double width;
		double height;
		double znear;
		double zfar;
	};

	struct ScissorRect {
		size_t height;
		size_t width;
		size_t x;
		size_t y;
	};

	enum class CPUCacheMode: size_t {
		DefaultCache  = 0,
		WriteCombined = 1,
	};
	INDIUM_BITFLAG_ENUM_CLASS(CPUCacheMode);

	enum class StorageMode: size_t {
		Shared = 0,
		Managed = 1,
		Private = 2,
		Memoryless = 3,
	};
	INDIUM_BITFLAG_ENUM_CLASS(StorageMode);

	enum class HazardTrackingMode: size_t {
		HazardTrackingModeDefault = 0,
		HazardTrackingModeUntracked = 1,
		HazardTrackingModeTracked = 2,
	};
	INDIUM_BITFLAG_ENUM_CLASS(HazardTrackingMode);

	enum class ResourceOptions: size_t {
		CPUCacheModeDefaultCache  = 0 << 0,
		CPUCacheModeWriteCombined = 1 << 0,

		StorageModeShared     = 0 << 4,
		StorageModeManaged    = 1 << 4,
		StorageModePrivate    = 2 << 4,
		StorageModeMemoryless = 3 << 4,

		HazardTrackingModeDefault   = 0 << 8,
		HazardTrackingModeUntracked = 1 << 8,
		HazardTrackingModeTracked   = 2 << 8,
	};
	INDIUM_BITFLAG_ENUM_CLASS(ResourceOptions);

	struct Origin {
		size_t x;
		size_t y;
		size_t z;
	};

	struct Size {
		size_t width;
		size_t height;
		size_t depth;
	};

	struct Region {
		Origin origin;
		Size size;

		static constexpr Region make2D(size_t x, size_t y, size_t width, size_t height) {
			return Region {
				Origin { x, y, 0 },
				Size { width, height, 1 },
			};
		};
	};

	enum class SamplerMinMagFilter: size_t {
		Nearest = 0,
		Linear = 1,
	};

	enum class SamplerMipFilter: size_t {
		NotMipmapped = 0,
		Nearest = 1,
		Linear = 2,
	};

	enum class SamplerAddressMode: size_t {
		ClampToEdge = 0,
		MirrorClampToEdge = 1,
		Repeat = 2,
		MirrorRepeat = 3,
		ClampToZero = 4,
		ClampToBorderColor = 5,
	};

	enum class SamplerBorderColor: size_t {
		TransparentBlack = 0,
		OpaqueBlack = 1,
		OpaqueWhite = 2,
	};

	enum class CompareFunction: size_t {
		Never = 0,
		Less = 1,
		Equal = 2,
		LessEqual = 3,
		Greater = 4,
		NotEqual = 5,
		GreaterEqual = 6,
		Always = 7,
	};

	enum class StencilOperation: size_t {
		Keep = 0,
		Zero = 1,
		Replace = 2,
		IncrementClamp = 3,
		DecrementClamp = 4,
		Invert = 5,
		IncrementWrap = 6,
		DecrementWrap = 7,
	};

	enum class IndexType: size_t {
		UInt16 = 0,
		UInt32 = 1,
	};

	enum class VisibilityResultMode: size_t {
		Disabled = 0,
		Boolean = 1,
		Counting = 2,
	};

	enum class ResourceUsage: size_t {
		Read = 1 << 0,
		Write = 1 << 1,

		/**
		 * @deprecated
		 */
		Sample = 1 << 2,
	};
	INDIUM_BITFLAG_ENUM_CLASS(ResourceUsage);

	enum class RenderStages: size_t {
		Vertex = 1 << 0,
		Fragment = 1 << 1,
		Tile = 1 << 2,
		Object = 1 << 3,
		Mesh = 1 << 4,
	};
	INDIUM_BITFLAG_ENUM_CLASS(RenderStages);

	enum class BlitOption: size_t {
		None = 0,
		DepthFromDepthStencil = 1 << 0,
		StencilFromDepthStencil = 1 << 1,
		RowLinearPVRTC = 1 << 2,
	};
	INDIUM_BITFLAG_ENUM_CLASS(BlitOption);

	enum class DispatchType: size_t {
		Serial = 0,
		Concurrent = 1,
	};

	enum class AttributeFormat: size_t {
		Invalid = 0,
		UChar2 = 1,
		UChar3 = 2,
		UChar4 = 3,
		Char2 = 4,
		Char3 = 5,
		Char4 = 6,
		UChar2Normalized = 7,
		UChar3Normalized = 8,
		UChar4Normalized = 9,
		Char2Normalized = 10,
		Char3Normalized = 11,
		Char4Normalized = 12,
		UShort2 = 13,
		UShort3 = 14,
		UShort4 = 15,
		Short2 = 16,
		Short3 = 17,
		Short4 = 18,
		UShort2Normalized = 19,
		UShort3Normalized = 20,
		UShort4Normalized = 21,
		Short2Normalized = 22,
		Short3Normalized = 23,
		Short4Normalized = 24,
		Half2 = 25,
		Half3 = 26,
		Half4 = 27,
		Float = 28,
		Float2 = 29,
		Float3 = 30,
		Float4 = 31,
		Int = 32,
		Int2 = 33,
		Int3 = 34,
		Int4 = 35,
		UInt = 36,
		UInt2 = 37,
		UInt3 = 38,
		UInt4 = 39,
		Int1010102Normalized = 40,
		UInt1010102Normalized = 41,
		UChar4Normalized_BGRA = 42,
		UChar = 45,
		Char = 46,
		UCharNormalized = 47,
		CharNormalized = 48,
		UShort = 49,
		Short = 50,
		UShortNormalized = 51,
		ShortNormalized = 52,
		Half = 53,
	};

	enum class StepFunction: size_t {
		Constant = 0,
		PerVertex = 1,
		PerInstance = 2,
		PerPatch = 3,
		PerPatchControlPoint = 4,
		ThreadPositionInGridX = 5,
		ThreadPositionInGridY = 6,
		ThreadPositionInGridXIndexed = 7,
		ThreadPositionInGridYIndexed = 8,
	};

	enum class PipelineOption: size_t {
		None = 0,
		ArgumentInfo = 1 << 0,
		BufferTypeInfo = 1 << 1,
		FailOnBinaryArchiveMiss = 1 << 2,
	};
	INDIUM_BITFLAG_ENUM_CLASS(PipelineOption);
};
