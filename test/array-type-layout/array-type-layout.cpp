#include "../../src/iridium/air.cpp"
#include <cstdio>
int main() {
    if (!Iridium::DynamicLLVM::init()) return 2;
    LLVMContextRef context = LLVMContextCreate();
    Iridium::SPIRV::Builder builder;
    auto array = LLVMArrayType2(LLVMVectorType(LLVMFloatTypeInContext(context), 3), 4);
    auto id = llvmTypeToSPIRVType(builder, array);
    auto type = builder.reverseLookupType(id);
    bool pass = type->size == 64 && type->alignment == 16;
    std::printf("%s array of four float3 columns size=%zu alignment=%zu\n", pass ? "PASS" : "FAIL", type->size, type->alignment);
    LLVMContextDispose(context);
    return pass ? 0 : 1;
}
