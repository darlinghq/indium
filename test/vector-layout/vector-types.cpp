#include "../../src/iridium/air.cpp"
#include <cstdio>
int main() {
    if (!Iridium::DynamicLLVM::init()) return 2;
    auto context = LLVMContextCreate();
    auto module = LLVMModuleCreateWithNameInContext("authored-vector-types", context);
    struct Scalar { const char *name; LLVMTypeRef type; } scalars[] = {
        {"float", LLVMFloatTypeInContext(context)}, {"half", LLVMHalfTypeInContext(context)},
        {"int", LLVMInt32TypeInContext(context)}, {"long", LLVMInt64TypeInContext(context)},
        {"short", LLVMInt16TypeInContext(context)}, {"char", LLVMInt8TypeInContext(context)}};
    unsigned failures = 0, checks = 0;
    for (const auto &scalar : scalars) for (unsigned count = 2; count <= 4; ++count) {
        Iridium::SPIRV::Builder namedBuilder, llvmBuilder;
        auto name = std::string(scalar.name) + std::to_string(count);
        auto namedID = spirvTypeForAIRTypeName(namedBuilder, module, name);
        auto llvmID = llvmTypeToSPIRVType(llvmBuilder, LLVMVectorType(scalar.type, count));
        auto named = namedBuilder.reverseLookupType(namedID);
        auto reference = llvmBuilder.reverseLookupType(llvmID);
        bool pass = named && reference && named->size == reference->size && named->alignment == reference->alignment;
        ++checks; failures += !pass;
        printf("%s %s alignment=%zu expected=%zu\n", pass ? "PASS" : "FAIL", name.c_str(), named ? named->alignment : 0, reference ? reference->alignment : 0);
    }
    for (const auto &scalar : scalars) for (unsigned count = 2; count <= 4; ++count) {
        Iridium::SPIRV::Builder builder;
        auto name = std::string("packed_") + scalar.name + std::to_string(count);
        auto id = spirvTypeForAIRTypeName(builder, module, name);
        auto type = builder.reverseLookupType(id);
        auto expected = (count == 2 ? 2 : 4) * std::max(type->size / count / 4, size_t(1));
        bool pass = type->alignment == expected;
        ++checks; failures += !pass;
        printf("%s %s preserved alignment=%zu\n", pass ? "PASS" : "FAIL", name.c_str(), type->alignment);
    }
    LLVMDisposeModule(module); LLVMContextDispose(context);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
