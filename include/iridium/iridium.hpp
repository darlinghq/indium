#pragma once

#include <cstddef>

namespace Iridium {
	void init();
	void finit();

	struct OutputInfo {

	};

	/**
	 * @returns A pointer allocated with `malloc`, or `nullptr` if translation failed.
	 */
	void* translate(const void* inputData, size_t inputSize, size_t& outputSize, OutputInfo& outputInfo);
};
