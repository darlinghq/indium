// Copyright (C) 2026 Darling Developers

#include <indium/library.private.hpp>

#include <utility>

namespace {
	constexpr size_t headerWords = 5;
	constexpr uint32_t opExecutionMode = 16;
	// SPV_KHR_float_controls: the SignedZeroInfNanPreserve execution mode takes the floating-point width as its literal.
	constexpr uint32_t executionModeSignedZeroInfNanPreserve = 4461;
}

uint32_t Indium::spirvSignedZeroInfNanPreserveWidths(const uint32_t* words, size_t wordCount) {
	uint32_t widths = 0;

	if (!words) {
		return widths;
	}

	for (size_t at = headerWords; at < wordCount;) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		if (count == 0 || count > wordCount - at) {
			break;
		}
		// OpExecutionMode <entry point> <mode> <literals...>; this mode has one literal, the width
		if (opcode == opExecutionMode && count == 4 && words[at + 2] == executionModeSignedZeroInfNanPreserve) {
			switch (words[at + 3]) {
				case 16: widths |= SignedZeroInfNanPreserve16; break;
				case 32: widths |= SignedZeroInfNanPreserve32; break;
				case 64: widths |= SignedZeroInfNanPreserve64; break;
				default: break;
			}
		}
		at += count;
	}

	return widths;
};

std::string Indium::unsupportedSignedZeroInfNanPreserve(const uint32_t* words, size_t wordCount, uint32_t supported) {
	uint32_t missing = spirvSignedZeroInfNanPreserveWidths(words, wordCount) & ~supported;
	std::string reason;

	const std::pair<uint32_t, const char*> widths[] = {
		{ SignedZeroInfNanPreserve16, "16" }, { SignedZeroInfNanPreserve32, "32" }, { SignedZeroInfNanPreserve64, "64" },
	};
	for (const auto& [bit, width]: widths) {
		if (missing & bit) {
			reason += reason.empty() ? "" : ", ";
			reason += std::string("shaderSignedZeroInfNanPreserveFloat") + width;
		}
	}

	if (reason.empty()) {
		return reason;
	}

	return "the module sets the SignedZeroInfNanPreserve execution mode, which this device does not support (" + reason + " is not reported)";
};
