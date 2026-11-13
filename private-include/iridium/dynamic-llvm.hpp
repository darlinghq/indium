#pragma once

#include <llvm-c/BitReader.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <dlfcn.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace Iridium {
	namespace DynamicLLVM {
		bool init();
		void finit();

		void* resolveSymbol(const char* name);

		struct DynamicFunctionBase {
			void* pointer;
			std::once_flag _resolveFlag;
			const char* name;

			DynamicFunctionBase(const char* _name):
				name(_name)
				{};

			bool resolve() {
				std::call_once(_resolveFlag, [&]() {
					pointer = resolveSymbol(name);
				});
				return !!pointer;
			};

			bool isAvailable() {
				return resolve();
			};

			operator bool() {
				return isAvailable();
			};
		};

		template<typename FuncPtr>
		struct DynamicFunction;

		template<typename Ret, typename... Arg>
		struct DynamicFunction<Ret(Arg...)>: public DynamicFunctionBase {
			DynamicFunction(const char* _name):
				DynamicFunctionBase(_name)
				{};

			Ret operator ()(Arg... arg) {
				if (!resolve()) {
					throw std::runtime_error(std::string("Failed to resolve ") + name);
				}
				Ret (*func)(Arg...) = reinterpret_cast<decltype(func)>(pointer);
				return func(std::forward<Arg>(arg)...);
			};
		};

		#define IRIDIUM_DYNAMICLLVM_FUNCTION_FOREACH(_macro) \
			_macro(LLVMBasicBlockAsValue) \
			_macro(LLVMConstIntGetSExtValue) \
			_macro(LLVMConstIntGetZExtValue) \
			_macro(LLVMConstRealGetDouble) \
			_macro(LLVMCountIncoming) \
			_macro(LLVMCountParamTypes) \
			_macro(LLVMCountStructElementTypes) \
			_macro(LLVMCreatePassBuilderOptions) \
			_macro(LLVMCreateMemoryBufferWithMemoryRange) \
			_macro(LLVMDisposeMemoryBuffer) \
			_macro(LLVMDisposeMessage) \
			_macro(LLVMDisposeErrorMessage) \
			_macro(LLVMDisposeModule) \
			_macro(LLVMDisposePassBuilderOptions) \
			_macro(LLVMGetAlignment) \
			_macro(LLVMGetArrayLength) \
			_macro(LLVMGetCalledValue) \
			_macro(LLVMGetCondition) \
			_macro(LLVMGetConstOpcode) \
			_macro(LLVMGetElementAsConstant) \
			_macro(LLVMGetElementType) \
			_macro(LLVMGetErrorMessage) \
			_macro(LLVMGetFCmpPredicate) \
			_macro(LLVMGetAllocatedType) \
			_macro(LLVMGetGEPSourceElementType) \
			_macro(LLVMGetICmpPredicate) \
			_macro(LLVMGetFirstBasicBlock) \
			_macro(LLVMGetFirstFunction) \
			_macro(LLVMGetNextFunction) \
			_macro(LLVMGetSection) \
			_macro(LLVMGetFirstInstruction) \
			_macro(LLVMGetFirstNamedMetadata) \
			_macro(LLVMGetIncomingBlock) \
			_macro(LLVMGetIncomingValue) \
			_macro(LLVMGetIndices) \
			_macro(LLVMGetInitializer) \
			_macro(LLVMGetInstructionOpcode) \
			_macro(LLVMGetIntTypeWidth) \
			_macro(LLVMGetLastInstruction) \
			_macro(LLVMGetMaskValue) \
			_macro(LLVMGetMDNodeNumOperands) \
			_macro(LLVMGetMDNodeOperands) \
			_macro(LLVMGetMDString) \
			_macro(LLVMGetNamedFunction) \
			_macro(LLVMGetNamedMetadata) \
			_macro(LLVMGetNamedMetadataNumOperands) \
			_macro(LLVMGetNamedMetadataOperands) \
			_macro(LLVMGetNextBasicBlock) \
			_macro(LLVMGetNextInstruction) \
			_macro(LLVMGetNumContainedTypes) \
			_macro(LLVMGetNumIndices) \
			_macro(LLVMGetNumMaskElements) \
			_macro(LLVMGetNumOperands) \
			_macro(LLVMGetNumSuccessors) \
			_macro(LLVMGetOperand) \
			_macro(LLVMGetParam) \
			_macro(LLVMGetParamTypes) \
			_macro(LLVMGetPointerAddressSpace) \
			_macro(LLVMGetReturnType) \
			_macro(LLVMGetSuccessor) \
			_macro(LLVMGetTarget) \
			_macro(LLVMGetTypeByName) \
			_macro(LLVMGetTypeKind) \
			_macro(LLVMGetUndefMaskElem) \
			_macro(LLVMGetValueKind) \
			_macro(LLVMGetValueName2) \
			_macro(LLVMGetVectorSize) \
			_macro(LLVMGlobalGetValueType) \
			_macro(LLVMIsAConstantInt) \
			_macro(LLVMIsAMDString) \
			_macro(LLVMIsConstant) \
			_macro(LLVMIsNull) \
			_macro(LLVMIsConditional) \
			_macro(LLVMIsPackedStruct) \
			_macro(LLVMParseBitcode2) \
			_macro(LLVMPrintModuleToString) \
			_macro(LLVMRunPasses) \
			_macro(LLVMSetTarget) \
			_macro(LLVMStructGetTypeAtIndex) \
			_macro(LLVMTypeOf)

		#define IRIDIUM_DYNAMICLLVM_FUNCTION_DECL(_name) \
			extern DynamicFunction<decltype(_name)> _name;

		IRIDIUM_DYNAMICLLVM_FUNCTION_FOREACH(IRIDIUM_DYNAMICLLVM_FUNCTION_DECL)
	};
};
