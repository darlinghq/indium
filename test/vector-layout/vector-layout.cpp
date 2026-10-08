#include "../../src/iridium/air.cpp"
#include <llvm-c/BitWriter.h>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) try {
    if (argc != 3 || !Iridium::DynamicLLVM::init()) return 2;
    bool vectorFirst = std::strcmp(argv[1], "vector-first") == 0;
    if (!vectorFirst && std::strcmp(argv[1], "scalar-first") != 0) return 2;
    auto context = LLVMContextCreate();
    auto module = LLVMModuleCreateWithNameInContext("authored-vector-layout", context);
    auto f32 = LLVMFloatTypeInContext(context);
    auto i32 = LLVMInt32TypeInContext(context);
    auto i64 = LLVMInt64TypeInContext(context);
    auto vector = LLVMVectorType(f32, 3);
    auto array = LLVMArrayType(vector, 2);
    auto structure = LLVMStructCreateNamed(context, "AuthoredVectorArray");
    LLVMStructSetBody(structure, &array, 1, false);
    auto pointer = LLVMPointerTypeInContext(context, 1);
    LLVMTypeRef parameters[] = {pointer, pointer, pointer};
    auto functionType = LLVMFunctionType(LLVMVoidTypeInContext(context), parameters, 3, false);
    auto function = LLVMAddFunction(module, "authoredVertex", functionType);
    auto writer = LLVMCreateBuilderInContext(context);
    LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(context, function, "entry"));
    LLVMValueRef indices[] = {LLVMConstInt(i64, 0, false), LLVMConstInt(i32, 0, false),
                              LLVMConstInt(i64, 1, false), LLVMConstInt(i64, 0, false)};
    auto address = LLVMBuildGEP2(writer, structure, LLVMGetParam(function, 1), indices, 4, "secondVectorX");
    auto value = LLVMBuildLoad2(writer, f32, address, "loaded");
    LLVMSetAlignment(value, 4);
    LLVMSetAlignment(LLVMBuildStore(writer, value, LLVMGetParam(function, 2)), 4);
    LLVMBuildRetVoid(writer);
    auto text = [&](const char *s) { return LLVMMDStringInContext(context, s, std::strlen(s)); };
    auto number = [&](unsigned n) { return LLVMConstInt(i32, n, false); };
    const char *names[] = {vectorFirst ? "float3" : "float", "AuthoredVectorArray", "float"};
    LLVMValueRef descriptors[3];
    for (unsigned i = 0; i < 3; ++i) {
        LLVMValueRef fields[] = {number(i), text("air.buffer"), text("air.location_index"), number(i),
                                number(1), text("air.arg_type_name"), text(names[i])};
        descriptors[i] = LLVMMDNodeInContext(context, fields, 7);
    }
    LLVMValueRef root[] = {function, LLVMMDNodeInContext(context, nullptr, 0), LLVMMDNodeInContext(context, descriptors, 3)};
    LLVMAddNamedMetadataOperand(module, "air.kernel", LLVMMDNodeInContext(context, root, 3));
    auto bitcode = LLVMWriteBitcodeToMemoryBuffer(module);
    Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Kernel, "authoredVertex",
        LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
    Iridium::SPIRV::Builder builder;
    builder.setVersion(1, 5);
    builder.requireCapability(Iridium::SPIRV::Capability::Shader);
    builder.requireCapability(Iridium::SPIRV::Capability::PhysicalStorageBufferAddresses);
    builder.requireCapability(Iridium::SPIRV::Capability::Int64);
    builder.setAddressingModel(Iridium::SPIRV::AddressingModel::PhysicalStorageBuffer64);
    builder.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);
    Iridium::OutputInfo info;
    translated.analyze(builder, info);
    size_t length;
    void *bytes = builder.finalize(length);
    FILE *output = std::fopen(argv[2], "wb");
    if (!output) return 3;
    bool written = std::fwrite(bytes, 1, length, output) == length;
    std::fclose(output); std::free(bytes);
    LLVMDisposeMemoryBuffer(bitcode); LLVMDisposeBuilder(writer);
    LLVMDisposeModule(module); LLVMContextDispose(context);
    return written ? 0 : 3;
} catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what()); return 1;
}
