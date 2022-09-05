#pragma once

#include <memory>

#include <llvm-c/Core.h>

namespace Iridium {
	namespace LLVMSupport {
		using Module = std::unique_ptr<LLVMOpaqueModule, decltype(&LLVMDisposeModule)>;
		using MemoryBuffer = std::unique_ptr<LLVMOpaqueMemoryBuffer, decltype(&LLVMDisposeMemoryBuffer)>;
	};
};
