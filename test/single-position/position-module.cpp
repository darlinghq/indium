#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <llvm-c/BitWriter.h>
int main(int argc, char **argv) try {
    if (argc != 3 || !Iridium::DynamicLLVM::init())
        return 2;
    bool invalidStage = std::strcmp(argv[1], "invalid-stage") == 0;
    bool invalidType = std::strcmp(argv[1], "invalid-type") == 0;
    bool ordinary = std::strcmp(argv[1], "ordinary") == 0;
    bool fragment = invalidStage || std::strcmp(argv[1], "fragment") == 0;
    bool structure = std::strcmp(argv[1], "struct") == 0;
    if (!fragment && !structure && !invalidType && !ordinary && std::strcmp(argv[1], "single") != 0)
        return 2;
    auto c = LLVMContextCreate();
    auto m = LLVMModuleCreateWithNameInContext("authored-position", c);
    auto f32 = LLVMFloatTypeInContext(c);
    auto i32 = LLVMInt32TypeInContext(c);
    auto v4 = LLVMVectorType(f32, 4);
    auto result = invalidType ? f32 : structure ? LLVMStructTypeInContext(c, &v4, 1, 0) : v4;
    auto ft = LLVMFunctionType(result, fragment ? nullptr : &v4, fragment ? 0 : 1, 0);
    const char *name = fragment ? "authoredFragment" : "authoredVertex";
    auto function = LLVMAddFunction(m, name, ft);
    auto writer = LLVMCreateBuilderInContext(c);
    LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
    auto value = fragment ? LLVMConstNull(v4) : LLVMGetParam(function, 0);
    if (fragment) {
        LLVMValueRef components[] = {LLVMConstReal(f32, 1), LLVMConstReal(f32, 0), LLVMConstReal(f32, 0), LLVMConstReal(f32, 1)};
        value = LLVMConstVector(components, 4);
    }
    if (invalidType)
        value = LLVMBuildExtractElement(writer, value, LLVMConstInt(i32, 0, 0), "scalar");
    if (structure)
        value = LLVMBuildInsertValue(writer, LLVMGetUndef(result), value, 0, "position");
    LLVMBuildRet(writer, value);
    auto str = [&](const char *s) { return LLVMMDStringInContext(c, s, std::strlen(s)); };
    auto n = [&](unsigned x) { return LLVMConstInt(i32, x, 0); };
    auto position = str("air.position");
    auto positionDescriptor = LLVMMDNodeInContext(c, &position, 1);
    LLVMValueRef descriptor[] = {n(0), str("air.vertex_input"), str("air.location_index"), n(0), n(1)};
    auto parameter = LLVMMDNodeInContext(c, descriptor, 5);
    bool positionOutput = invalidStage || (!fragment && !ordinary);
    LLVMValueRef root[] = {function, LLVMMDNodeInContext(c, positionOutput ? &positionDescriptor : nullptr, positionOutput ? 1 : 0),
                           LLVMMDNodeInContext(c, fragment ? nullptr : &parameter, fragment ? 0 : 1)};
    LLVMAddNamedMetadataOperand(m, fragment ? "air.fragment" : "air.vertex", LLVMMDNodeInContext(c, root, 3));
    auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);
    Iridium::AIR::Function translated(fragment ? Iridium::AIR::Function::Type::Fragment : Iridium::AIR::Function::Type::Vertex, name, LLVMGetBufferStart(bitcode),
                                      LLVMGetBufferSize(bitcode));
    Iridium::SPIRV::Builder b;
    b.setVersion(1, 5);
    b.requireCapability(Iridium::SPIRV::Capability::Shader);
    b.requireCapability(Iridium::SPIRV::Capability::PhysicalStorageBufferAddresses);
    b.setAddressingModel(Iridium::SPIRV::AddressingModel::PhysicalStorageBuffer64);
    b.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);
    Iridium::OutputInfo info;
    translated.analyze(b, info);
    size_t size;
    void *bytes = b.finalize(size);
    FILE *f = std::fopen(argv[2], "wb");
    if (!f)
        return 3;
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
