#pragma once

#include <cstddef>

namespace Indium {
	void init(const char** additionalExtensions = nullptr, size_t additionalExtensionCount = 0, bool enableValidation = false);
	void finit();
};
