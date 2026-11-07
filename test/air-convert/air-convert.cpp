#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <llvm-c/BitWriter.h>
// Authored vertex function calling air.convert.f.<to>.f.<from> on a constant.
// usage: air-convert <to> <from> <out.spv>, each of f16 f32 f64 or v4f16 v4f32 v4f64
static LLVMTypeRef floating(LLVMContextRef c, const std::string &name) {
    bool vector = name[0] == 'v';
    auto scalarName = vector ? name.substr(2) : name;
    auto scalar = scalarName == "f16" ? LLVMHalfTypeInContext(c) : scalarName == "f32" ? LLVMFloatTypeInContext(c) : LLVMDoubleTypeInContext(c);
    return vector ? LLVMVectorType(scalar, 4) : scalar;
}
int main(int argc, char **argv) try {
    if (argc != 4 || !Iridium::DynamicLLVM::init())
        return 2;
    std::string to = argv[1], from = argv[2];
    auto c = LLVMContextCreate();
    auto m = LLVMModuleCreateWithNameInContext("authored-air-convert", c);
    auto toType = floating(c, to), fromType = floating(c, from);
    auto convert = LLVMAddFunction(m, ("air.convert.f." + to + ".f." + from).c_str(), LLVMFunctionType(toType, &fromType, 1, 0));
    auto function = LLVMAddFunction(m, "authoredVertex", LLVMFunctionType(LLVMVoidTypeInContext(c), nullptr, 0, 0));
    auto writer = LLVMCreateBuilderInContext(c);
    LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
    auto value = LLVMConstNull(fromType);
    LLVMBuildCall2(writer, LLVMGlobalGetValueType(convert), convert, &value, 1, "converted");
    LLVMBuildRetVoid(writer);
    LLVMValueRef root[] = {function, LLVMMDNodeInContext(c, nullptr, 0), LLVMMDNodeInContext(c, nullptr, 0)};
    LLVMAddNamedMetadataOperand(m, "air.vertex", LLVMMDNodeInContext(c, root, 3));
    auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);
    Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Vertex, "authoredVertex", LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
    Iridium::SPIRV::Builder b;
    b.setVersion(1, 5);
    b.requireCapability(Iridium::SPIRV::Capability::Shader);
    b.requireCapability(Iridium::SPIRV::Capability::Float16);
    b.requireCapability(Iridium::SPIRV::Capability::Float64);
    b.setAddressingModel(Iridium::SPIRV::AddressingModel::Logical);
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
    std::printf("translated air.convert.f.%s.f.%s\n", to.c_str(), from.c_str());
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
