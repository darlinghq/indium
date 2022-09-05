#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <memory>

#include <llvm-c/Core.h>

#include <iridium/llvm.hpp>

namespace Iridium {
	namespace SPIRV {
		class Builder;
	};

	namespace AIR {
		class Library;

		class Function {
		public:
			enum class Type: uint8_t {
				Vertex = 0,
				Fragment = 1,
				Kernel = 2,
				Unqualified = 3,
				Visible = 4,
				Extern = 5,
				Intersection = 6,
			};

		private:
			LLVMSupport::Module _module { nullptr, LLVMDisposeModule };
			LLVMSupport::MemoryBuffer _bitcodeBuffer { nullptr, LLVMDisposeMemoryBuffer };
			LLVMValueRef _function;
			std::string _name;
			Type _type;

		public:
			Function(Type type, const std::string& name, const void* bitcode, size_t bitcodeSize);

			void analyze(SPIRV::Builder& builder) const;
			void emitBody(SPIRV::Builder& builder) const;
		};

		class Library {
			friend class Function;

		private:
			std::unordered_map<std::string, Function> _functions;

		public:
			Library(const void* data, size_t size);

			const Function* getFunction(const std::string& name) const;

			void buildModule(SPIRV::Builder& builder) const;
		};
	};
};
