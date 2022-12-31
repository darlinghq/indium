#pragma once

#include <memory>

#include <llvm-c/Core.h>
#include <iridium/dynamic-llvm.hpp>

namespace Iridium {
	namespace LLVMSupport {
		using Module = std::unique_ptr<LLVMOpaqueModule, std::reference_wrapper<DynamicLLVM::DynamicFunction<decltype(LLVMDisposeModule)>>>;
		using MemoryBuffer = std::unique_ptr<LLVMOpaqueMemoryBuffer, std::reference_wrapper<DynamicLLVM::DynamicFunction<decltype(LLVMDisposeMemoryBuffer)>>>;
	};
};
