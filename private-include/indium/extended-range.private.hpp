// Copyright (C) 2026 Lubos Dolezel
//
// The extended-range ("XR") pixel formats of Metal: BGRA10_XR, BGRA10_XR_sRGB, BGR10_XR and BGR10_XR_sRGB.
//
// What the formats store, from the public documentation of MTLPixelFormat:
//
//  - BGRA10_XR and BGRA10_XR_sRGB take 8 bytes per texel: four 16-bit chunks, blue first (the least
//    significant), then green, red and alpha. The 10-bit code of a component sits in the upper bits (6 to 15)
//    of its chunk, the 6 lower bits are padding and zero.
//  - BGR10_XR and BGR10_XR_sRGB take 4 bytes per texel: blue in bits 0 to 9, green in 10 to 19, red in 20 to 29,
//    and bits 30 and 31 are padding and zero. There is no alpha, which a shader reads as 1.
//  - The code of a color component maps to a float with (code - 384) / 510, which covers about -0.752941 to
//    1.25098 (codes 0 to 1022 are what rendering produces; the documentation names the upper end 1.25098, and a
//    code of 1023 that memory holds reads as 1.25294 by the same formula). The sRGB variants apply the
//    sRGB transfer function to that number when reading, and its inverse when writing, so the shader sees (and
//    writes) linear values. The transfer function is odd-symmetric around zero for values below zero, which
//    gives the documented range of -0.5271 to 1.66894 of the shader float.
//  - The alpha component uses the same code mapping, is never transformed by the sRGB curve, and is clamped to
//    0 to 1 whenever it is sampled, rendered or written.
//
// Vulkan has no such format, so a texture of these formats is an R32G32B32A32_SFLOAT image holding the value the
// shader sees. A float keeps every value of the 10-bit codes exactly (so does it after the sRGB curve), sampling
// filters and blending work on shader values like they do in Metal, and the codes only appear where bytes cross
// the API: replaceRegion, getBytes and the rest of the copies between the image and memory.
// A render pass rounds what it wrote to the codes afterwards (see ExtendedRangeKernel), which is what makes
// the image hold only values a code can have, within the range of the format.
//
// Not documented, and so assumed (no Metal device was available to observe them): conversions to codes round to
// nearest even; a NaN converts to 0; padding bits that are not zero are ignored when reading and written as
// zero; an alpha code outside 384 to 894 reads clamped, so it is not kept by a round trip through bytes.
#pragma once

#include <indium/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Indium::ExtendedRange {
	constexpr float codeOffset = 384.0f;
	constexpr float codeScale = 510.0f;
	// the codes the range of the format ends at when a value is rendered, and the highest a memory can hold
	constexpr uint32_t maxRenderedCode = 1022;
	constexpr uint32_t maxStoredCode = 1023;
	constexpr uint32_t maxAlphaCode = 894;
	constexpr uint32_t minAlphaCode = 384;

	constexpr bool isExtendedRange(PixelFormat pixelFormat) {
		return pixelFormat == PixelFormat::BGRA10_XR || pixelFormat == PixelFormat::BGRA10_XR_sRGB
			|| pixelFormat == PixelFormat::BGR10_XR || pixelFormat == PixelFormat::BGR10_XR_sRGB;
	};

	constexpr bool hasAlpha(PixelFormat pixelFormat) {
		return pixelFormat == PixelFormat::BGRA10_XR || pixelFormat == PixelFormat::BGRA10_XR_sRGB;
	};

	constexpr bool isSRGB(PixelFormat pixelFormat) {
		return pixelFormat == PixelFormat::BGRA10_XR_sRGB || pixelFormat == PixelFormat::BGR10_XR_sRGB;
	};

	/** The size of a texel in memory (not in the image). */
	constexpr size_t bytesPerTexel(PixelFormat pixelFormat) {
		return hasAlpha(pixelFormat) ? 8 : 4;
	};

	inline float srgbToLinear(float encoded) {
		float magnitude = std::fabs(encoded);
		float linear = magnitude <= 0.04045f ? magnitude / 12.92f : std::pow((magnitude + 0.055f) / 1.055f, 2.4f);
		return std::copysign(linear, encoded);
	};

	inline float linearToSrgb(float linear) {
		float magnitude = std::fabs(linear);
		float encoded = magnitude <= 0.0031308f ? magnitude * 12.92f : 1.055f * std::pow(magnitude, 1.0f / 2.4f) - 0.055f;
		return std::copysign(encoded, linear);
	};

	/** The float a code of a color component stands for, before the sRGB curve. */
	inline float codeToEncoded(uint32_t code) {
		return (static_cast<float>(code) - codeOffset) / codeScale;
	};

	/** The shader value of a color code. */
	inline float decodeColor(uint32_t code, bool srgb) {
		float encoded = codeToEncoded(code);
		return srgb ? srgbToLinear(encoded) : encoded;
	};

	/** The shader value of an alpha code (clamped to 0 to 1). */
	inline float decodeAlpha(uint32_t code) {
		return std::clamp(codeToEncoded(code), 0.0f, 1.0f);
	};

	inline uint32_t roundToCode(float encoded, uint32_t low, uint32_t high) {
		float code = std::nearbyint(encoded * codeScale + codeOffset);
		return static_cast<uint32_t>(std::clamp(code, static_cast<float>(low), static_cast<float>(high)));
	};

	/** The code a shader value of a color component is stored as, at most `highest`. */
	inline uint32_t encodeColor(float value, bool srgb, uint32_t highest) {
		if (std::isnan(value)) {
			value = 0.0f;
		}
		return roundToCode(srgb ? linearToSrgb(value) : value, 0, highest);
	};

	/** The code a shader value of the alpha component is stored as. */
	inline uint32_t encodeAlpha(float value) {
		if (std::isnan(value)) {
			value = 0.0f;
		}
		return roundToCode(value, minAlphaCode, maxAlphaCode);
	};

	/** What a stored texel reads as, for texels of memory in `pixelFormat`: red, green, blue, alpha. */
	inline void unpackTexel(PixelFormat pixelFormat, const uint8_t* source, float* rgba) {
		uint32_t blue, green, red, alpha = 0;
		if (hasAlpha(pixelFormat)) {
			uint16_t chunks[4];
			std::memcpy(chunks, source, sizeof(chunks));
			blue = chunks[0] >> 6;
			green = chunks[1] >> 6;
			red = chunks[2] >> 6;
			alpha = chunks[3] >> 6;
		} else {
			uint32_t word;
			std::memcpy(&word, source, sizeof(word));
			blue = word & 0x3ff;
			green = (word >> 10) & 0x3ff;
			red = (word >> 20) & 0x3ff;
		}
		bool srgb = isSRGB(pixelFormat);
		rgba[0] = decodeColor(red, srgb);
		rgba[1] = decodeColor(green, srgb);
		rgba[2] = decodeColor(blue, srgb);
		rgba[3] = hasAlpha(pixelFormat) ? decodeAlpha(alpha) : 1.0f;
	};

	/** The memory of a texel that holds the shader value `rgba`. */
	inline void packTexel(PixelFormat pixelFormat, const float* rgba, uint8_t* destination) {
		bool srgb = isSRGB(pixelFormat);
		uint32_t red = encodeColor(rgba[0], srgb, maxStoredCode);
		uint32_t green = encodeColor(rgba[1], srgb, maxStoredCode);
		uint32_t blue = encodeColor(rgba[2], srgb, maxStoredCode);
		if (hasAlpha(pixelFormat)) {
			uint16_t chunks[4] = {
				static_cast<uint16_t>(blue << 6),
				static_cast<uint16_t>(green << 6),
				static_cast<uint16_t>(red << 6),
				static_cast<uint16_t>(encodeAlpha(rgba[3]) << 6),
			};
			std::memcpy(destination, chunks, sizeof(chunks));
		} else {
			uint32_t word = blue | (green << 10) | (red << 20);
			std::memcpy(destination, &word, sizeof(word));
		}
	};

	/** The value a texel holds after it was stored: the shader value rounded to a code, within the range. */
	inline void quantizeTexel(PixelFormat pixelFormat, float* rgba) {
		bool srgb = isSRGB(pixelFormat);
		for (int i = 0; i < 3; ++i) {
			rgba[i] = decodeColor(encodeColor(rgba[i], srgb, maxRenderedCode), srgb);
		}
		rgba[3] = hasAlpha(pixelFormat) ? decodeAlpha(encodeAlpha(rgba[3])) : 1.0f;
	};
};
