#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <llvm-c/BitWriter.h>
int main(int argc, char **argv) try {
    if ((argc != 4 && argc != 5) || !Iridium::DynamicLLVM::init())
        return 2;
    bool compute = argc == 5;
    unsigned width = std::strtoul(argv[1], nullptr, 10);
    if (width != 1 && width != 8 && width != 16 && width != 32 && width != 64 && width != 128)
        return 3;
    long index = std::strtol(argv[2], nullptr, 10);
    auto c = LLVMContextCreate();
    auto m = LLVMModuleCreateWithNameInContext("authored-gep-index", c);
    auto ptr = LLVMPointerTypeInContext(c, 1);
    auto f32 = LLVMFloatTypeInContext(c);
    auto i32 = LLVMInt32TypeInContext(c);
    auto it = LLVMIntTypeInContext(c, width);
    LLVMTypeRef params[] = {ptr, ptr};
    auto ft = LLVMFunctionType(LLVMVoidTypeInContext(c), params, compute ? 2 : 1, 0);
    auto function = LLVMAddFunction(m, "authoredVertex", ft);
    auto writer = LLVMCreateBuilderInContext(c);
    LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
    auto offset = LLVMConstInt(it, index, 1);
    auto address = LLVMBuildGEP2(writer, f32, LLVMGetParam(function, 0), &offset, 1, "offsetAddress");
    auto value = LLVMBuildLoad2(writer, f32, address, "loaded");
    LLVMSetAlignment(value, 4);
    if (compute) {
        auto store = LLVMBuildStore(writer, value, LLVMGetParam(function, 1));
        LLVMSetAlignment(store, 4);
    } else
        LLVMBuildFAdd(writer, value, LLVMConstReal(f32, 1.0), "used");
    LLVMBuildRetVoid(writer);
    auto str = [&](const char *s) { return LLVMMDStringInContext(c, s, std::strlen(s)); };
    auto n = [&](unsigned x) { return LLVMConstInt(i32, x, 0); };
    LLVMValueRef descriptor[] = {
        n(0), str("air.buffer"), str("air.location_index"), n(0), n(1), str("air.arg_type_name"), str("float")};
    auto parameter = LLVMMDNodeInContext(c, descriptor, 7);
    LLVMValueRef outDescriptor[] = {
        n(1), str("air.buffer"), str("air.location_index"), n(1), n(1), str("air.arg_type_name"), str("float")};
    LLVMValueRef descriptors[] = {parameter, LLVMMDNodeInContext(c, outDescriptor, 7)};
    LLVMValueRef root[] = {function, LLVMMDNodeInContext(c, nullptr, 0),
                           LLVMMDNodeInContext(c, descriptors, compute ? 2 : 1)};
    LLVMAddNamedMetadataOperand(m, compute ? "air.kernel" : "air.vertex", LLVMMDNodeInContext(c, root, 3));
    auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);
    Iridium::AIR::Function translated(compute ? Iridium::AIR::Function::Type::Kernel
                                              : Iridium::AIR::Function::Type::Vertex,
                                      "authoredVertex", LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
    Iridium::SPIRV::Builder b;
    b.setVersion(1, 5);
    b.requireCapability(Iridium::SPIRV::Capability::Shader);
    b.requireCapability(Iridium::SPIRV::Capability::PhysicalStorageBufferAddresses);
    b.requireCapability(Iridium::SPIRV::Capability::Int64);
    b.setAddressingModel(Iridium::SPIRV::AddressingModel::PhysicalStorageBuffer64);
    b.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);
    Iridium::OutputInfo info;
    translated.analyze(b, info);
    size_t size;
    void *bytes = b.finalize(size);
    FILE *f = std::fopen(argv[3], "wb");
    if (!f)
        return 4;
    std::fwrite(bytes, 1, size, f);
    std::fclose(f);
    std::free(bytes);
    LLVMDisposeMemoryBuffer(bitcode);
    LLVMDisposeBuilder(writer);
    LLVMDisposeModule(m);
    LLVMContextDispose(c);
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
