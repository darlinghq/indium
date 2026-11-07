#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <llvm-c/BitWriter.h>
// Authored fragment function that applies a getelementptr to its texture argument,
// which translates to an image pointer, not a physical buffer pointer.
int main(int argc, char **argv) try {
    if (argc != 2 || !Iridium::DynamicLLVM::init())
        return 2;
    auto c = LLVMContextCreate();
    auto m = LLVMModuleCreateWithNameInContext("authored-nonphysical-gep", c);
    auto ptr1 = LLVMPointerTypeInContext(c, 1);
    auto i8 = LLVMInt8TypeInContext(c);
    auto i32 = LLVMInt32TypeInContext(c);
    auto i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef params[] = {ptr1};
    auto function = LLVMAddFunction(m, "authoredFragment", LLVMFunctionType(LLVMVoidTypeInContext(c), params, 1, 0));
    auto writer = LLVMCreateBuilderInContext(c);
    LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
    LLVMValueRef index[] = {LLVMConstInt(i64, 4, 1)};
    LLVMBuildGEP2(writer, i8, LLVMGetParam(function, 0), index, 1, "offset");
    LLVMBuildRetVoid(writer);
    auto str = [&](const char *s) { return LLVMMDStringInContext(c, s, std::strlen(s)); };
    auto n = [&](unsigned x) { return LLVMConstInt(i32, x, 0); };
    LLVMValueRef texture[] = {n(0), str("air.texture"), str("air.location_index"), n(0), n(1), str("air.sample"),
                              str("air.arg_type_name"), str("texture2d<float, sample>")};
    LLVMValueRef descriptors[] = {LLVMMDNodeInContext(c, texture, 8)};
    LLVMValueRef root[] = {function, LLVMMDNodeInContext(c, nullptr, 0), LLVMMDNodeInContext(c, descriptors, 1)};
    LLVMAddNamedMetadataOperand(m, "air.fragment", LLVMMDNodeInContext(c, root, 3));
    auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);
    Iridium::SPIRV::Builder b;
    b.setVersion(1, 5);
    b.requireCapability(Iridium::SPIRV::Capability::Shader);
    b.requireCapability(Iridium::SPIRV::Capability::PhysicalStorageBufferAddresses);
    b.requireCapability(Iridium::SPIRV::Capability::Int64);
    b.setAddressingModel(Iridium::SPIRV::AddressingModel::PhysicalStorageBuffer64);
    b.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);
    Iridium::OutputInfo info;
    Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Fragment, "authoredFragment", LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
    translated.analyze(b, info);
    size_t size;
    void *bytes = b.finalize(size);
    FILE *f = std::fopen(argv[1], "wb");
    if (!f)
        return 4;
    std::fwrite(bytes, 1, size, f);
    std::fclose(f);
    std::free(bytes);
    std::printf("translated\n");
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
