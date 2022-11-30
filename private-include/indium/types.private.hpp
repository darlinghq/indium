#pragma once

#include <indium/types.hpp>

#include <vulkan/vulkan.h>

#include <memory>

namespace Indium {
	class PrivateDevice;

	class BadEnumValue: public std::exception {};

	struct TimelineSemaphore {
		std::shared_ptr<PrivateDevice> device;
		VkSemaphore semaphore;
		uint64_t count;
	};

	struct BinarySemaphore {
		std::shared_ptr<PrivateDevice> device;
		VkSemaphore semaphore;
	};

	static constexpr VkComponentSwizzle textureSwizzleToVkComponentSwizzle(TextureSwizzle swizzle) {
		switch (swizzle) {
			case TextureSwizzle::Zero:  return VK_COMPONENT_SWIZZLE_ZERO;
			case TextureSwizzle::One:   return VK_COMPONENT_SWIZZLE_ONE;
			case TextureSwizzle::Red:   return VK_COMPONENT_SWIZZLE_R;
			case TextureSwizzle::Green: return VK_COMPONENT_SWIZZLE_G;
			case TextureSwizzle::Blue:  return VK_COMPONENT_SWIZZLE_B;
			case TextureSwizzle::Alpha: return VK_COMPONENT_SWIZZLE_A;
		}
		throw BadEnumValue();
	};

	static constexpr VkFormat pixelFormatToVkFormat(PixelFormat pixelFormat) {
		switch (pixelFormat) {
			case PixelFormat::Invalid:               return VK_FORMAT_UNDEFINED;
			case PixelFormat::A8Unorm:               return VK_FORMAT_UNDEFINED;
			case PixelFormat::R8Unorm:               return VK_FORMAT_R8_UNORM;
			case PixelFormat::R8Unorm_sRGB:          return VK_FORMAT_R8_SRGB;
			case PixelFormat::R8Snorm:               return VK_FORMAT_R8_SNORM;
			case PixelFormat::R8Uint:                return VK_FORMAT_R8_UINT;
			case PixelFormat::R8Sint:                return VK_FORMAT_R8_SINT;
			case PixelFormat::R16Unorm:              return VK_FORMAT_R16_UNORM;
			case PixelFormat::R16Snorm:              return VK_FORMAT_R16_SNORM;
			case PixelFormat::R16Uint:               return VK_FORMAT_R16_UINT;
			case PixelFormat::R16Sint:               return VK_FORMAT_R16_SINT;
			case PixelFormat::R16Float:              return VK_FORMAT_R16_SFLOAT;
			case PixelFormat::RG8Unorm:              return VK_FORMAT_R8G8_UNORM;
			case PixelFormat::RG8Unorm_sRGB:         return VK_FORMAT_R8G8_SRGB;
			case PixelFormat::RG8Snorm:              return VK_FORMAT_R8G8_SNORM;
			case PixelFormat::RG8Uint:               return VK_FORMAT_R8G8_UINT;
			case PixelFormat::RG8Sint:               return VK_FORMAT_R8G8_SINT;
			case PixelFormat::B5G6R5Unorm:           return VK_FORMAT_R5G6B5_UNORM_PACK16;
			case PixelFormat::A1BGR5Unorm:           return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
			case PixelFormat::ABGR4Unorm:            return VK_FORMAT_A4B4G4R4_UNORM_PACK16;
			case PixelFormat::BGR5A1Unorm:           return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
			case PixelFormat::R32Uint:               return VK_FORMAT_R32_UINT;
			case PixelFormat::R32Sint:               return VK_FORMAT_R32_SINT;
			case PixelFormat::R32Float:              return VK_FORMAT_R32_SFLOAT;
			case PixelFormat::RG16Unorm:             return VK_FORMAT_R16G16_UNORM;
			case PixelFormat::RG16Snorm:             return VK_FORMAT_R16G16_SNORM;
			case PixelFormat::RG16Uint:              return VK_FORMAT_R16G16_UINT;
			case PixelFormat::RG16Sint:              return VK_FORMAT_R16G16_SINT;
			case PixelFormat::RG16Float:             return VK_FORMAT_R16G16_SFLOAT;
			case PixelFormat::RGBA8Unorm:            return VK_FORMAT_R8G8B8A8_UNORM;
			case PixelFormat::RGBA8Unorm_sRGB:       return VK_FORMAT_R8G8B8A8_SRGB;
			case PixelFormat::RGBA8Snorm:            return VK_FORMAT_R8G8B8A8_SNORM;
			case PixelFormat::RGBA8Uint:             return VK_FORMAT_R8G8B8A8_UINT;
			case PixelFormat::RGBA8Sint:             return VK_FORMAT_R8G8B8A8_SINT;
			case PixelFormat::BGRA8Unorm:            return VK_FORMAT_B8G8R8A8_UNORM;
			case PixelFormat::BGRA8Unorm_sRGB:       return VK_FORMAT_B8G8R8A8_SRGB;
			case PixelFormat::RGB10A2Unorm:          return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			case PixelFormat::RGB10A2Uint:           return VK_FORMAT_A2B10G10R10_UINT_PACK32;
			case PixelFormat::RG11B10Float:          return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			case PixelFormat::RGB9E5Float:           return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
			case PixelFormat::BGR10A2Unorm:          return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
			case PixelFormat::RG32Uint:              return VK_FORMAT_R32G32_UINT;
			case PixelFormat::RG32Sint:              return VK_FORMAT_R32G32_SINT;
			case PixelFormat::RG32Float:             return VK_FORMAT_R32G32_SFLOAT;
			case PixelFormat::RGBA16Unorm:           return VK_FORMAT_R16G16B16A16_UNORM;
			case PixelFormat::RGBA16Snorm:           return VK_FORMAT_R16G16B16A16_SNORM;
			case PixelFormat::RGBA16Uint:            return VK_FORMAT_R16G16B16A16_UINT;
			case PixelFormat::RGBA16Sint:            return VK_FORMAT_R16G16B16A16_SINT;
			case PixelFormat::RGBA16Float:           return VK_FORMAT_R16G16B16A16_SFLOAT;
			case PixelFormat::RGBA32Uint:            return VK_FORMAT_R32G32B32A32_UINT;
			case PixelFormat::RGBA32Sint:            return VK_FORMAT_R32G32B32A32_SINT;
			case PixelFormat::RGBA32Float:           return VK_FORMAT_R32G32B32A32_SFLOAT;
			case PixelFormat::BC1_RGBA:              return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			case PixelFormat::BC1_RGBA_sRGB:         return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
			case PixelFormat::BC2_RGBA:              return VK_FORMAT_BC2_UNORM_BLOCK;
			case PixelFormat::BC2_RGBA_sRGB:         return VK_FORMAT_BC2_SRGB_BLOCK;
			case PixelFormat::BC3_RGBA:              return VK_FORMAT_BC3_UNORM_BLOCK;
			case PixelFormat::BC3_RGBA_sRGB:         return VK_FORMAT_BC3_SRGB_BLOCK;
			case PixelFormat::BC4_RUnorm:            return VK_FORMAT_BC4_UNORM_BLOCK;
			case PixelFormat::BC4_RSnorm:            return VK_FORMAT_BC4_SNORM_BLOCK;
			case PixelFormat::BC5_RGUnorm:           return VK_FORMAT_BC5_UNORM_BLOCK;
			case PixelFormat::BC5_RGSnorm:           return VK_FORMAT_BC5_SNORM_BLOCK;
			case PixelFormat::BC6H_RGBFloat:         return VK_FORMAT_BC6H_SFLOAT_BLOCK;
			case PixelFormat::BC6H_RGBUfloat:        return VK_FORMAT_BC6H_UFLOAT_BLOCK;
			case PixelFormat::BC7_RGBAUnorm:         return VK_FORMAT_BC7_UNORM_BLOCK;
			case PixelFormat::BC7_RGBAUnorm_sRGB:    return VK_FORMAT_BC7_SRGB_BLOCK;
			case PixelFormat::PVRTC_RGB_2BPP:        return VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG;
			case PixelFormat::PVRTC_RGB_2BPP_sRGB:   return VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG;
			case PixelFormat::PVRTC_RGB_4BPP:        return VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG;
			case PixelFormat::PVRTC_RGB_4BPP_sRGB:   return VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG;
			case PixelFormat::PVRTC_RGBA_2BPP:       return VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG;
			case PixelFormat::PVRTC_RGBA_2BPP_sRGB:  return VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG;
			case PixelFormat::PVRTC_RGBA_4BPP:       return VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG;
			case PixelFormat::PVRTC_RGBA_4BPP_sRGB:  return VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG;
			case PixelFormat::EAC_R11Unorm:          return VK_FORMAT_EAC_R11_UNORM_BLOCK;
			case PixelFormat::EAC_R11Snorm:          return VK_FORMAT_EAC_R11_SNORM_BLOCK;
			case PixelFormat::EAC_RG11Unorm:         return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
			case PixelFormat::EAC_RG11Snorm:         return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;
			case PixelFormat::EAC_RGBA8:             return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
			case PixelFormat::EAC_RGBA8_sRGB:        return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
			case PixelFormat::ETC2_RGB8:             return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
			case PixelFormat::ETC2_RGB8_sRGB:        return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
			case PixelFormat::ETC2_RGB8A1:           return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
			case PixelFormat::ETC2_RGB8A1_sRGB:      return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK;
			case PixelFormat::ASTC_4x4_sRGB:         return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
			case PixelFormat::ASTC_5x4_sRGB:         return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
			case PixelFormat::ASTC_5x5_sRGB:         return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
			case PixelFormat::ASTC_6x5_sRGB:         return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
			case PixelFormat::ASTC_6x6_sRGB:         return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
			case PixelFormat::ASTC_8x5_sRGB:         return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
			case PixelFormat::ASTC_8x6_sRGB:         return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
			case PixelFormat::ASTC_8x8_sRGB:         return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
			case PixelFormat::ASTC_10x5_sRGB:        return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
			case PixelFormat::ASTC_10x6_sRGB:        return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
			case PixelFormat::ASTC_10x8_sRGB:        return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
			case PixelFormat::ASTC_10x10_sRGB:       return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
			case PixelFormat::ASTC_12x10_sRGB:       return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
			case PixelFormat::ASTC_12x12_sRGB:       return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
			case PixelFormat::ASTC_4x4_LDR:          return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
			case PixelFormat::ASTC_5x4_LDR:          return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
			case PixelFormat::ASTC_5x5_LDR:          return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
			case PixelFormat::ASTC_6x5_LDR:          return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
			case PixelFormat::ASTC_6x6_LDR:          return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
			case PixelFormat::ASTC_8x5_LDR:          return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
			case PixelFormat::ASTC_8x6_LDR:          return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
			case PixelFormat::ASTC_8x8_LDR:          return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
			case PixelFormat::ASTC_10x5_LDR:         return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
			case PixelFormat::ASTC_10x6_LDR:         return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
			case PixelFormat::ASTC_10x8_LDR:         return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
			case PixelFormat::ASTC_10x10_LDR:        return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
			case PixelFormat::ASTC_12x10_LDR:        return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
			case PixelFormat::ASTC_12x12_LDR:        return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
			case PixelFormat::ASTC_4x4_HDR:          return VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK;
			case PixelFormat::ASTC_5x4_HDR:          return VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK;
			case PixelFormat::ASTC_5x5_HDR:          return VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK;
			case PixelFormat::ASTC_6x5_HDR:          return VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK;
			case PixelFormat::ASTC_6x6_HDR:          return VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK;
			case PixelFormat::ASTC_8x5_HDR:          return VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK;
			case PixelFormat::ASTC_8x6_HDR:          return VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK;
			case PixelFormat::ASTC_8x8_HDR:          return VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK;
			case PixelFormat::ASTC_10x5_HDR:         return VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK;
			case PixelFormat::ASTC_10x6_HDR:         return VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK;
			case PixelFormat::ASTC_10x8_HDR:         return VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK;
			case PixelFormat::ASTC_10x10_HDR:        return VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK;
			case PixelFormat::ASTC_12x10_HDR:        return VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK;
			case PixelFormat::ASTC_12x12_HDR:        return VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK;
			case PixelFormat::GBGR422:               return VK_FORMAT_G8B8G8R8_422_UNORM;
			case PixelFormat::BGRG422:               return VK_FORMAT_B8G8R8G8_422_UNORM;
			case PixelFormat::Depth16Unorm:          return VK_FORMAT_D16_UNORM;
			case PixelFormat::Depth32Float:          return VK_FORMAT_D32_SFLOAT;
			case PixelFormat::Stencil8:              return VK_FORMAT_S8_UINT;
			case PixelFormat::Depth24Unorm_Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
			case PixelFormat::Depth32Float_Stencil8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
			case PixelFormat::X32_Stencil8:          return VK_FORMAT_UNDEFINED;
			case PixelFormat::X24_Stencil8:          return VK_FORMAT_UNDEFINED;
			case PixelFormat::BGRA10_XR:             return VK_FORMAT_UNDEFINED;
			case PixelFormat::BGRA10_XR_sRGB:        return VK_FORMAT_UNDEFINED;
			case PixelFormat::BGR10_XR:              return VK_FORMAT_UNDEFINED;
			case PixelFormat::BGR10_XR_sRGB:         return VK_FORMAT_UNDEFINED;

			default:
				return VK_FORMAT_UNDEFINED;
		}
	};

	// this does NOT handle compressed formats
	static constexpr size_t pixelFormatToByteCount(PixelFormat pixelFormat) {
		switch (pixelFormat) {
			case PixelFormat::Invalid:               return 0;
			case PixelFormat::A8Unorm:               return 0;
			case PixelFormat::R8Unorm:               return 1;
			case PixelFormat::R8Unorm_sRGB:          return 1;
			case PixelFormat::R8Snorm:               return 1;
			case PixelFormat::R8Uint:                return 1;
			case PixelFormat::R8Sint:                return 1;
			case PixelFormat::R16Unorm:              return 2;
			case PixelFormat::R16Snorm:              return 2;
			case PixelFormat::R16Uint:               return 2;
			case PixelFormat::R16Sint:               return 2;
			case PixelFormat::R16Float:              return 2;
			case PixelFormat::RG8Unorm:              return 2;
			case PixelFormat::RG8Unorm_sRGB:         return 2;
			case PixelFormat::RG8Snorm:              return 2;
			case PixelFormat::RG8Uint:               return 2;
			case PixelFormat::RG8Sint:               return 2;
			case PixelFormat::B5G6R5Unorm:           return 2;
			case PixelFormat::A1BGR5Unorm:           return 2;
			case PixelFormat::ABGR4Unorm:            return 2;
			case PixelFormat::BGR5A1Unorm:           return 2;
			case PixelFormat::R32Uint:               return 4;
			case PixelFormat::R32Sint:               return 4;
			case PixelFormat::R32Float:              return 4;
			case PixelFormat::RG16Unorm:             return 4;
			case PixelFormat::RG16Snorm:             return 4;
			case PixelFormat::RG16Uint:              return 4;
			case PixelFormat::RG16Sint:              return 4;
			case PixelFormat::RG16Float:             return 4;
			case PixelFormat::RGBA8Unorm:            return 4;
			case PixelFormat::RGBA8Unorm_sRGB:       return 4;
			case PixelFormat::RGBA8Snorm:            return 4;
			case PixelFormat::RGBA8Uint:             return 4;
			case PixelFormat::RGBA8Sint:             return 4;
			case PixelFormat::BGRA8Unorm:            return 4;
			case PixelFormat::BGRA8Unorm_sRGB:       return 4;
			case PixelFormat::RGB10A2Unorm:          return 4;
			case PixelFormat::RGB10A2Uint:           return 4;
			case PixelFormat::RG11B10Float:          return 4;
			case PixelFormat::RGB9E5Float:           return 4;
			case PixelFormat::BGR10A2Unorm:          return 4;
			case PixelFormat::RG32Uint:              return 8;
			case PixelFormat::RG32Sint:              return 8;
			case PixelFormat::RG32Float:             return 8;
			case PixelFormat::RGBA16Unorm:           return 8;
			case PixelFormat::RGBA16Snorm:           return 8;
			case PixelFormat::RGBA16Uint:            return 8;
			case PixelFormat::RGBA16Sint:            return 8;
			case PixelFormat::RGBA16Float:           return 8;
			case PixelFormat::RGBA32Uint:            return 16;
			case PixelFormat::RGBA32Sint:            return 16;
			case PixelFormat::RGBA32Float:           return 16;

			default:
				return 0;
		}
	};

	static constexpr bool pixelFormatIsCompressed(PixelFormat pixelFormat) {
		switch (pixelFormat) {
			case PixelFormat::R8Unorm:
			case PixelFormat::R8Unorm_sRGB:
			case PixelFormat::R8Snorm:
			case PixelFormat::R8Uint:
			case PixelFormat::R8Sint:
			case PixelFormat::R16Unorm:
			case PixelFormat::R16Snorm:
			case PixelFormat::R16Uint:
			case PixelFormat::R16Sint:
			case PixelFormat::R16Float:
			case PixelFormat::RG8Unorm:
			case PixelFormat::RG8Unorm_sRGB:
			case PixelFormat::RG8Snorm:
			case PixelFormat::RG8Uint:
			case PixelFormat::RG8Sint:
			case PixelFormat::B5G6R5Unorm:
			case PixelFormat::A1BGR5Unorm:
			case PixelFormat::ABGR4Unorm:
			case PixelFormat::BGR5A1Unorm:
			case PixelFormat::R32Uint:
			case PixelFormat::R32Sint:
			case PixelFormat::R32Float:
			case PixelFormat::RG16Unorm:
			case PixelFormat::RG16Snorm:
			case PixelFormat::RG16Uint:
			case PixelFormat::RG16Sint:
			case PixelFormat::RG16Float:
			case PixelFormat::RGBA8Unorm:
			case PixelFormat::RGBA8Unorm_sRGB:
			case PixelFormat::RGBA8Snorm:
			case PixelFormat::RGBA8Uint:
			case PixelFormat::RGBA8Sint:
			case PixelFormat::BGRA8Unorm:
			case PixelFormat::BGRA8Unorm_sRGB:
			case PixelFormat::RGB10A2Unorm:
			case PixelFormat::RGB10A2Uint:
			case PixelFormat::RG11B10Float:
			case PixelFormat::RGB9E5Float:
			case PixelFormat::BGR10A2Unorm:
			case PixelFormat::RG32Uint:
			case PixelFormat::RG32Sint:
			case PixelFormat::RG32Float:
			case PixelFormat::RGBA16Unorm:
			case PixelFormat::RGBA16Snorm:
			case PixelFormat::RGBA16Uint:
			case PixelFormat::RGBA16Sint:
			case PixelFormat::RGBA16Float:
			case PixelFormat::RGBA32Uint:
			case PixelFormat::RGBA32Sint:
			case PixelFormat::RGBA32Float:
				return false;

			// assume any other format (including ones we don't know about) is compressed
			default:
				return true;
		}
	};

	static constexpr VkImageAspectFlags pixelFormatToVkImageAspectFlags(PixelFormat pixelFormat) {
		switch (pixelFormat) {
			case PixelFormat::A8Unorm:
			case PixelFormat::R8Unorm:
			case PixelFormat::R8Unorm_sRGB:
			case PixelFormat::R8Snorm:
			case PixelFormat::R8Uint:
			case PixelFormat::R8Sint:
			case PixelFormat::R16Unorm:
			case PixelFormat::R16Snorm:
			case PixelFormat::R16Uint:
			case PixelFormat::R16Sint:
			case PixelFormat::R16Float:
			case PixelFormat::RG8Unorm:
			case PixelFormat::RG8Unorm_sRGB:
			case PixelFormat::RG8Snorm:
			case PixelFormat::RG8Uint:
			case PixelFormat::RG8Sint:
			case PixelFormat::B5G6R5Unorm:
			case PixelFormat::A1BGR5Unorm:
			case PixelFormat::ABGR4Unorm:
			case PixelFormat::BGR5A1Unorm:
			case PixelFormat::R32Uint:
			case PixelFormat::R32Sint:
			case PixelFormat::R32Float:
			case PixelFormat::RG16Unorm:
			case PixelFormat::RG16Snorm:
			case PixelFormat::RG16Uint:
			case PixelFormat::RG16Sint:
			case PixelFormat::RG16Float:
			case PixelFormat::RGBA8Unorm:
			case PixelFormat::RGBA8Unorm_sRGB:
			case PixelFormat::RGBA8Snorm:
			case PixelFormat::RGBA8Uint:
			case PixelFormat::RGBA8Sint:
			case PixelFormat::BGRA8Unorm:
			case PixelFormat::BGRA8Unorm_sRGB:
			case PixelFormat::RGB10A2Unorm:
			case PixelFormat::RGB10A2Uint:
			case PixelFormat::RG11B10Float:
			case PixelFormat::RGB9E5Float:
			case PixelFormat::BGR10A2Unorm:
			case PixelFormat::RG32Uint:
			case PixelFormat::RG32Sint:
			case PixelFormat::RG32Float:
			case PixelFormat::RGBA16Unorm:
			case PixelFormat::RGBA16Snorm:
			case PixelFormat::RGBA16Uint:
			case PixelFormat::RGBA16Sint:
			case PixelFormat::RGBA16Float:
			case PixelFormat::RGBA32Uint:
			case PixelFormat::RGBA32Sint:
			case PixelFormat::RGBA32Float:
			case PixelFormat::BC1_RGBA:
			case PixelFormat::BC1_RGBA_sRGB:
			case PixelFormat::BC2_RGBA:
			case PixelFormat::BC2_RGBA_sRGB:
			case PixelFormat::BC3_RGBA:
			case PixelFormat::BC3_RGBA_sRGB:
			case PixelFormat::BC4_RUnorm:
			case PixelFormat::BC4_RSnorm:
			case PixelFormat::BC5_RGUnorm:
			case PixelFormat::BC5_RGSnorm:
			case PixelFormat::BC6H_RGBFloat:
			case PixelFormat::BC6H_RGBUfloat:
			case PixelFormat::BC7_RGBAUnorm:
			case PixelFormat::BC7_RGBAUnorm_sRGB:
			case PixelFormat::PVRTC_RGB_2BPP:
			case PixelFormat::PVRTC_RGB_2BPP_sRGB:
			case PixelFormat::PVRTC_RGB_4BPP:
			case PixelFormat::PVRTC_RGB_4BPP_sRGB:
			case PixelFormat::PVRTC_RGBA_2BPP:
			case PixelFormat::PVRTC_RGBA_2BPP_sRGB:
			case PixelFormat::PVRTC_RGBA_4BPP:
			case PixelFormat::PVRTC_RGBA_4BPP_sRGB:
			case PixelFormat::EAC_R11Unorm:
			case PixelFormat::EAC_R11Snorm:
			case PixelFormat::EAC_RG11Unorm:
			case PixelFormat::EAC_RG11Snorm:
			case PixelFormat::EAC_RGBA8:
			case PixelFormat::EAC_RGBA8_sRGB:
			case PixelFormat::ETC2_RGB8:
			case PixelFormat::ETC2_RGB8_sRGB:
			case PixelFormat::ETC2_RGB8A1:
			case PixelFormat::ETC2_RGB8A1_sRGB:
			case PixelFormat::ASTC_4x4_sRGB:
			case PixelFormat::ASTC_5x4_sRGB:
			case PixelFormat::ASTC_5x5_sRGB:
			case PixelFormat::ASTC_6x5_sRGB:
			case PixelFormat::ASTC_6x6_sRGB:
			case PixelFormat::ASTC_8x5_sRGB:
			case PixelFormat::ASTC_8x6_sRGB:
			case PixelFormat::ASTC_8x8_sRGB:
			case PixelFormat::ASTC_10x5_sRGB:
			case PixelFormat::ASTC_10x6_sRGB:
			case PixelFormat::ASTC_10x8_sRGB:
			case PixelFormat::ASTC_10x10_sRGB:
			case PixelFormat::ASTC_12x10_sRGB:
			case PixelFormat::ASTC_12x12_sRGB:
			case PixelFormat::ASTC_4x4_LDR:
			case PixelFormat::ASTC_5x4_LDR:
			case PixelFormat::ASTC_5x5_LDR:
			case PixelFormat::ASTC_6x5_LDR:
			case PixelFormat::ASTC_6x6_LDR:
			case PixelFormat::ASTC_8x5_LDR:
			case PixelFormat::ASTC_8x6_LDR:
			case PixelFormat::ASTC_8x8_LDR:
			case PixelFormat::ASTC_10x5_LDR:
			case PixelFormat::ASTC_10x6_LDR:
			case PixelFormat::ASTC_10x8_LDR:
			case PixelFormat::ASTC_10x10_LDR:
			case PixelFormat::ASTC_12x10_LDR:
			case PixelFormat::ASTC_12x12_LDR:
			case PixelFormat::ASTC_4x4_HDR:
			case PixelFormat::ASTC_5x4_HDR:
			case PixelFormat::ASTC_5x5_HDR:
			case PixelFormat::ASTC_6x5_HDR:
			case PixelFormat::ASTC_6x6_HDR:
			case PixelFormat::ASTC_8x5_HDR:
			case PixelFormat::ASTC_8x6_HDR:
			case PixelFormat::ASTC_8x8_HDR:
			case PixelFormat::ASTC_10x5_HDR:
			case PixelFormat::ASTC_10x6_HDR:
			case PixelFormat::ASTC_10x8_HDR:
			case PixelFormat::ASTC_10x10_HDR:
			case PixelFormat::ASTC_12x10_HDR:
			case PixelFormat::ASTC_12x12_HDR:
			case PixelFormat::GBGR422:
			case PixelFormat::BGRG422:
			case PixelFormat::BGRA10_XR:
			case PixelFormat::BGRA10_XR_sRGB:
			case PixelFormat::BGR10_XR:
			case PixelFormat::BGR10_XR_sRGB:
				return VK_IMAGE_ASPECT_COLOR_BIT;

			case PixelFormat::Depth16Unorm:
			case PixelFormat::Depth32Float:
				return VK_IMAGE_ASPECT_DEPTH_BIT;

			case PixelFormat::Stencil8:
			case PixelFormat::X32_Stencil8:
			case PixelFormat::X24_Stencil8:
				return VK_IMAGE_ASPECT_STENCIL_BIT;

			case PixelFormat::Depth24Unorm_Stencil8:
			case PixelFormat::Depth32Float_Stencil8:
				return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

			case PixelFormat::Invalid:
			default:
				return VK_IMAGE_ASPECT_NONE;
		}
	};

	static constexpr VkColorComponentFlags colorWriteMaskToVkColorComponentFlags(ColorWriteMask mask) {
		VkColorComponentFlags flags = 0;
		if (!!(mask & ColorWriteMask::Alpha)) {
			flags |= VK_COLOR_COMPONENT_A_BIT;
		}
		if (!!(mask & ColorWriteMask::Blue)) {
			flags |= VK_COLOR_COMPONENT_B_BIT;
		}
		if (!!(mask & ColorWriteMask::Green)) {
			flags |= VK_COLOR_COMPONENT_G_BIT;
		}
		if (!!(mask & ColorWriteMask::Red)) {
			flags |= VK_COLOR_COMPONENT_R_BIT;
		}
		return flags;
	};

	static constexpr VkBlendOp blendOperationToVkBlendOp(BlendOperation op) {
		switch (op) {
			case BlendOperation::Add:             return VK_BLEND_OP_ADD;
			case BlendOperation::Subtract:        return VK_BLEND_OP_SUBTRACT;
			case BlendOperation::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
			case BlendOperation::Min:             return VK_BLEND_OP_MIN;
			case BlendOperation::Max:             return VK_BLEND_OP_MAX;
		}
		throw BadEnumValue();
	};

	static constexpr VkBlendFactor blendFactorToVkBlendFactor(BlendFactor factor) {
		switch (factor) {
			case BlendFactor::Zero:                     return VK_BLEND_FACTOR_ZERO;
			case BlendFactor::One:                      return VK_BLEND_FACTOR_ONE;
			case BlendFactor::SourceColor:              return VK_BLEND_FACTOR_SRC_COLOR;
			case BlendFactor::OneMinusSourceColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
			case BlendFactor::SourceAlpha:              return VK_BLEND_FACTOR_SRC_ALPHA;
			case BlendFactor::OneMinusSourceAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			case BlendFactor::DestinationColor:         return VK_BLEND_FACTOR_DST_COLOR;
			case BlendFactor::OneMinusDestinationColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
			case BlendFactor::DestinationAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
			case BlendFactor::OneMinusDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			case BlendFactor::SourceAlphaSaturated:     return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
			case BlendFactor::BlendColor:               return VK_BLEND_FACTOR_CONSTANT_COLOR;
			case BlendFactor::OneMinusBlendColor:       return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
			case BlendFactor::BlendAlpha:               return VK_BLEND_FACTOR_CONSTANT_ALPHA;
			case BlendFactor::OneMinusBlendAlpha:       return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
			case BlendFactor::Source1Color:             return VK_BLEND_FACTOR_SRC1_COLOR;
			case BlendFactor::OneMinusSource1Color:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
			case BlendFactor::Source1Alpha:             return VK_BLEND_FACTOR_SRC1_ALPHA;
			case BlendFactor::OneMinusSource1Alpha:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
		}
		throw BadEnumValue();
	};

	static constexpr VkAttachmentLoadOp loadActionToVkAttachmentLoadOp(LoadAction loadAction, bool isColorAttachment) {
		switch (loadAction) {
			case LoadAction::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			case LoadAction::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
			case LoadAction::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
			case LoadAction::Default:  return isColorAttachment ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
		}
		throw BadEnumValue();
	};

	static constexpr VkAttachmentStoreOp storeActionToVkAttachmentStoreOp(StoreAction storeAction, bool isColorAttachment) {
		switch (storeAction) {
			case StoreAction::DontCare:                   return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			case StoreAction::Store:                      return VK_ATTACHMENT_STORE_OP_STORE;
			case StoreAction::MultisampleResolve:         return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			case StoreAction::StoreAndMultisampleResolve: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			case StoreAction::Unknown:                    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			case StoreAction::CustomSampleDepthStore:     return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			case StoreAction::Default:                    return isColorAttachment ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		throw BadEnumValue();
	};

	static constexpr VkImageViewType textureTypeToVkImageViewType(TextureType format) {
		switch (format) {
			case TextureType::e1D:                 return VK_IMAGE_VIEW_TYPE_1D;
			case TextureType::e1DArray:            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			case TextureType::e2D:                 return VK_IMAGE_VIEW_TYPE_2D;
			case TextureType::e2DArray:            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case TextureType::e2DMultisample:      return VK_IMAGE_VIEW_TYPE_2D;
			case TextureType::eCube:               return VK_IMAGE_VIEW_TYPE_CUBE;
			case TextureType::eCubeArray:          return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			case TextureType::e3D:                 return VK_IMAGE_VIEW_TYPE_3D;
			case TextureType::e2DMultisampleArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case TextureType::eTextureBuffer:      return VK_IMAGE_VIEW_TYPE_1D; // ?
		}
		throw BadEnumValue();
	};

	static constexpr VkImageType textureTypeToVkImageType(TextureType format) {
		switch (format) {
			case TextureType::e1D:                 return VK_IMAGE_TYPE_1D;
			case TextureType::e1DArray:            return VK_IMAGE_TYPE_1D;
			case TextureType::e2D:                 return VK_IMAGE_TYPE_2D;
			case TextureType::e2DArray:            return VK_IMAGE_TYPE_2D;
			case TextureType::e2DMultisample:      return VK_IMAGE_TYPE_2D;
			case TextureType::eCube:               return VK_IMAGE_TYPE_2D;
			case TextureType::eCubeArray:          return VK_IMAGE_TYPE_2D;
			case TextureType::e3D:                 return VK_IMAGE_TYPE_3D;
			case TextureType::e2DMultisampleArray: return VK_IMAGE_TYPE_2D;
			case TextureType::eTextureBuffer:      return VK_IMAGE_TYPE_1D; // ?
		}
		throw BadEnumValue();
	};

	static constexpr VkCullModeFlags cullModeToVkCullMode(CullMode cullMode) {
		VkCullModeFlags flags = 0;
		if (!!(cullMode & CullMode::Front)) {
			flags |= VK_CULL_MODE_FRONT_BIT;
		}
		if (!!(cullMode & CullMode::Back)) {
			flags |= VK_CULL_MODE_BACK_BIT;
		}
		return flags;
	};

	static constexpr VkFrontFace windingToVkFrontFace(Winding winding) {
		switch (winding) {
			case Winding::Clockwise:        return VK_FRONT_FACE_CLOCKWISE;
			case Winding::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		}
		throw BadEnumValue();
	};

	static constexpr VkPrimitiveTopology primitiveTypeToVkPrimitiveTopology(PrimitiveType primitiveType) {
		switch (primitiveType) {
			case PrimitiveType::Point:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case PrimitiveType::Line:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case PrimitiveType::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case PrimitiveType::Triangle:      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

			default: throw BadEnumValue();
		}
	};

	static constexpr VkCompareOp compareFunctionToVkCompareOp(CompareFunction compareFunction) {
		switch (compareFunction) {
			case CompareFunction::Never:        return VK_COMPARE_OP_NEVER;
			case CompareFunction::Less:         return VK_COMPARE_OP_LESS;
			case CompareFunction::Equal:        return VK_COMPARE_OP_EQUAL;
			case CompareFunction::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
			case CompareFunction::Greater:      return VK_COMPARE_OP_GREATER;
			case CompareFunction::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
			case CompareFunction::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case CompareFunction::Always:       return VK_COMPARE_OP_ALWAYS;
		}
		throw BadEnumValue();
	};

	static constexpr VkFilter samplerMinMagFilterToVkFilter(SamplerMinMagFilter samplerMinMagFilter) {
		switch (samplerMinMagFilter) {
			case SamplerMinMagFilter::Nearest: return VK_FILTER_NEAREST;
			case SamplerMinMagFilter::Linear:  return VK_FILTER_LINEAR;
		}
		throw BadEnumValue();
	};

	static constexpr VkSamplerMipmapMode samplerMipFilterToVkSamplerMipmapMode(SamplerMipFilter samplerMipFilter) {
		switch (samplerMipFilter) {
			case SamplerMipFilter::NotMipmapped: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
			case SamplerMipFilter::Nearest:      return VK_SAMPLER_MIPMAP_MODE_NEAREST;
			case SamplerMipFilter::Linear:       return VK_SAMPLER_MIPMAP_MODE_LINEAR;
		}
		throw BadEnumValue();
	};

	static constexpr VkSamplerAddressMode samplerAddressModeToVkSamplerAddressMode(SamplerAddressMode samplerAddressMode) {
		switch (samplerAddressMode) {
			case SamplerAddressMode::ClampToEdge:        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case SamplerAddressMode::MirrorClampToEdge:  return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
			case SamplerAddressMode::Repeat:             return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			case SamplerAddressMode::MirrorRepeat:       return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case SamplerAddressMode::ClampToZero:        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			case SamplerAddressMode::ClampToBorderColor: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		}
		throw BadEnumValue();
	};

	static constexpr VkBorderColor samplerBorderColorToVkBorderColor(SamplerBorderColor samplerBorderColor) {
		// XXX: not sure if these should be floats or ints
		switch (samplerBorderColor) {
			case SamplerBorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			case SamplerBorderColor::OpaqueBlack:      return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			case SamplerBorderColor::OpaqueWhite:      return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		}
		throw BadEnumValue();
	};

	static constexpr VkStencilOp stencilOperationToVkStencilOp(StencilOperation stencilOperation) {
		switch (stencilOperation) {
			case StencilOperation::Keep:           return VK_STENCIL_OP_KEEP;
			case StencilOperation::Zero:           return VK_STENCIL_OP_ZERO;
			case StencilOperation::Replace:        return VK_STENCIL_OP_REPLACE;
			case StencilOperation::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
			case StencilOperation::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
			case StencilOperation::Invert:         return VK_STENCIL_OP_INVERT;
			case StencilOperation::IncrementWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
			case StencilOperation::DecrementWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		}
		throw BadEnumValue();
	};

	static constexpr VkIndexType indexTypeToVkIndexType(IndexType indexType) {
		switch (indexType) {
			case IndexType::UInt16: return VK_INDEX_TYPE_UINT16;
			case IndexType::UInt32: return VK_INDEX_TYPE_UINT32;
		}
		throw BadEnumValue();
	};

	static constexpr VkFormat vertexFormatToVkFormat(VertexFormat vertexFormat) {
		switch (vertexFormat) {
			case VertexFormat::Invalid:               return VK_FORMAT_UNDEFINED;
			case VertexFormat::UChar2:                return VK_FORMAT_R8G8_UINT;
			case VertexFormat::UChar3:                return VK_FORMAT_R8G8B8_UINT;
			case VertexFormat::UChar4:                return VK_FORMAT_R8G8B8A8_UINT;
			case VertexFormat::Char2:                 return VK_FORMAT_R8G8_SINT;
			case VertexFormat::Char3:                 return VK_FORMAT_R8G8B8_SINT;
			case VertexFormat::Char4:                 return VK_FORMAT_R8G8B8A8_SINT;
			case VertexFormat::UChar2Normalized:      return VK_FORMAT_R8G8_UNORM;
			case VertexFormat::UChar3Normalized:      return VK_FORMAT_R8G8B8_UNORM;
			case VertexFormat::UChar4Normalized:      return VK_FORMAT_R8G8B8A8_UNORM;
			case VertexFormat::Char2Normalized:       return VK_FORMAT_R8G8_SNORM;
			case VertexFormat::Char3Normalized:       return VK_FORMAT_R8G8B8_SNORM;
			case VertexFormat::Char4Normalized:       return VK_FORMAT_R8G8B8A8_SNORM;
			case VertexFormat::UShort2:               return VK_FORMAT_R16G16_UINT;
			case VertexFormat::UShort3:               return VK_FORMAT_R16G16B16_UINT;
			case VertexFormat::UShort4:               return VK_FORMAT_R16G16B16A16_UINT;
			case VertexFormat::Short2:                return VK_FORMAT_R16G16_SINT;
			case VertexFormat::Short3:                return VK_FORMAT_R16G16B16_SINT;
			case VertexFormat::Short4:                return VK_FORMAT_R16G16B16A16_SINT;
			case VertexFormat::UShort2Normalized:     return VK_FORMAT_R16G16_UNORM;
			case VertexFormat::UShort3Normalized:     return VK_FORMAT_R16G16B16_UNORM;
			case VertexFormat::UShort4Normalized:     return VK_FORMAT_R16G16B16A16_UNORM;
			case VertexFormat::Short2Normalized:      return VK_FORMAT_R16G16_SNORM;
			case VertexFormat::Short3Normalized:      return VK_FORMAT_R16G16B16_SNORM;
			case VertexFormat::Short4Normalized:      return VK_FORMAT_R16G16B16A16_SNORM;
			case VertexFormat::Half2:                 return VK_FORMAT_R16G16_SFLOAT;
			case VertexFormat::Half3:                 return VK_FORMAT_R16G16B16_SFLOAT;
			case VertexFormat::Half4:                 return VK_FORMAT_R16G16B16A16_SFLOAT;
			case VertexFormat::Float:                 return VK_FORMAT_R32_SFLOAT;
			case VertexFormat::Float2:                return VK_FORMAT_R32G32_SFLOAT;
			case VertexFormat::Float3:                return VK_FORMAT_R32G32B32_SFLOAT;
			case VertexFormat::Float4:                return VK_FORMAT_R32G32B32A32_SFLOAT;
			case VertexFormat::Int:                   return VK_FORMAT_R32_SINT;
			case VertexFormat::Int2:                  return VK_FORMAT_R32G32_SINT;
			case VertexFormat::Int3:                  return VK_FORMAT_R32G32B32_SINT;
			case VertexFormat::Int4:                  return VK_FORMAT_R32G32B32A32_SINT;
			case VertexFormat::UInt:                  return VK_FORMAT_R32_UINT;
			case VertexFormat::UInt2:                 return VK_FORMAT_R32G32_UINT;
			case VertexFormat::UInt3:                 return VK_FORMAT_R32G32B32_UINT;
			case VertexFormat::UInt4:                 return VK_FORMAT_R32G32B32A32_UINT;
			case VertexFormat::Int1010102Normalized:  return VK_FORMAT_A2R10G10B10_SINT_PACK32;
			case VertexFormat::UInt1010102Normalized: return VK_FORMAT_A2R10G10B10_UINT_PACK32;
			case VertexFormat::UChar4Normalized_BGRA: return VK_FORMAT_B8G8R8A8_UNORM;
			case VertexFormat::UChar:                 return VK_FORMAT_R8_UINT;
			case VertexFormat::Char:                  return VK_FORMAT_R8_SINT;
			case VertexFormat::UCharNormalized:       return VK_FORMAT_R8_UNORM;
			case VertexFormat::CharNormalized:        return VK_FORMAT_R8_SNORM;
			case VertexFormat::UShort:                return VK_FORMAT_R16_UINT;
			case VertexFormat::Short:                 return VK_FORMAT_R16_SINT;
			case VertexFormat::UShortNormalized:      return VK_FORMAT_R16_UNORM;
			case VertexFormat::ShortNormalized:       return VK_FORMAT_R16_SNORM;
			case VertexFormat::Half:                  return VK_FORMAT_R16_SFLOAT;
		}
		throw BadEnumValue();
	};
};
