#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <llvm-c/Target.h>
// Every way of naming a vector must yield one SPIR-V type with LLVM's layout under the
// AIR datalayout, whichever declares it first: packed_T<N>, then T<N>, then <N x T>.
static const char *kAIRDataLayout =
    "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-"
    "v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-"
    "v1024:1024:1024-n8:16:32";
int main() {
    if (!Iridium::DynamicLLVM::init()) return 2;
    auto context = LLVMContextCreate();
    auto module = LLVMModuleCreateWithNameInContext("authored-vector-types", context);
    auto td = LLVMCreateTargetData(kAIRDataLayout);
    unsigned failures = 0, checks = 0;
    auto check = [&](const char *what, Iridium::SPIRV::Builder &builder, Iridium::SPIRV::ResultID id, LLVMTypeRef reference, bool sameID) {
        auto type = builder.reverseLookupType(id);
        bool pass = sameID && type && type->size == LLVMStoreSizeOfType(td, reference) &&
            type->alignment == LLVMABIAlignmentOfType(td, reference) && allocationSize(*type) == LLVMABISizeOfType(td, reference);
        ++checks; failures += !pass;
        printf("%s %s size=%zu alignment=%zu expected=%llu/%u%s\n", pass ? "PASS" : "FAIL", what,
            type ? type->size : 0, type ? type->alignment : 0, LLVMStoreSizeOfType(td, reference),
            LLVMABIAlignmentOfType(td, reference), sameID ? "" : " (distinct ids)");
    };
    struct Scalar { const char *name; LLVMTypeRef type; } scalars[] = {
        {"float", LLVMFloatTypeInContext(context)}, {"half", LLVMHalfTypeInContext(context)},
        {"int", LLVMInt32TypeInContext(context)}, {"long", LLVMInt64TypeInContext(context)},
        {"short", LLVMInt16TypeInContext(context)}, {"char", LLVMInt8TypeInContext(context)}};
    for (const auto &scalar : scalars) for (unsigned count = 2; count <= 4; ++count) {
        Iridium::SPIRV::Builder builder;
        auto name = std::string(scalar.name) + std::to_string(count);
        auto packedID = spirvTypeForAIRTypeName(builder, module, "packed_" + name);
        auto namedID = spirvTypeForAIRTypeName(builder, module, name);
        auto vector = LLVMVectorType(scalar.type, count);
        auto llvmID = llvmTypeToSPIRVType(builder, vector);
        check(("packed_" + name).c_str(), builder, packedID, vector, packedID == namedID && namedID == llvmID);
    }
    struct Wide { const char *name; LLVMTypeRef type; } wides[] = {
        {"<8 x float>", LLVMVectorType(LLVMFloatTypeInContext(context), 8)},
        {"<16 x i8>", LLVMVectorType(LLVMInt8TypeInContext(context), 16)}};
    for (const auto &wide : wides) {
        Iridium::SPIRV::Builder builder;
        check(wide.name, builder, llvmTypeToSPIRVType(builder, wide.type), wide.type, true);
    }
    LLVMDisposeTargetData(td);
    LLVMDisposeModule(module); LLVMContextDispose(context);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
