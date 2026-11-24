#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <iridium/iridium.hpp>

namespace Iridium {
	/**
	 * Reads the reflection document mslc writes next to a SPIR-V module (reflection_version 2)
	 * into the same FunctionInfo that the AIR path produces, one entry per entry point.
	 *
	 * A binding kind this reader does not know is skipped, as mslc's contract allows. A kind it
	 * knows but indium cannot bind (TextureBuffer, ThreadgroupMemory), and any malformed or
	 * inconsistent entry, fails the whole document.
	 *
	 * Needs no LLVM.
	 *
	 * @returns false and sets `error` if the document is rejected; `outputInfo` is then unspecified.
	 */
	bool parseMslcReflection(const char* json, size_t length, OutputInfo& outputInfo, std::string& error);
};
