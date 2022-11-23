#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

#include <llvm-c/Core.h>

#include <iridium/llvm.hpp>
#include <iridium/spirv.hpp>
#include <iridium/iridium.hpp>

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
			size_t _positionOutputIndex = SIZE_MAX;
			size_t _vertexIDInputIndex = SIZE_MAX;
			std::vector<SPIRV::ResultID> _outputValueIDs;
			std::vector<SPIRV::ResultID> _parameterIDs;

		public:
			Function(Type type, const std::string& name, const void* bitcode, size_t bitcodeSize);

			void analyze(SPIRV::Builder& builder, OutputInfo& outputInfo);
		};

		class Library {
			friend class Function;

		private:
			std::unordered_map<std::string, Function> _functions;
			OutputInfo _outputInfo;

		public:
			Library(const void* data, size_t size);

			const Function* getFunction(const std::string& name) const;

			void buildModule(SPIRV::Builder& builder, OutputInfo& outputInfo);
		};
	};
};
