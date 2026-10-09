#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <llvm-c/BitWriter.h>
// Authored kernel: loads from a float4x4 buffer through a getelementptr whose
// source element type is not the buffer's declared pointee, and stores it.
//   vector: <4 x float> source, index 1 (a column)
//   scalar: float source, index 5 (one element)
//   array:  [4 x <4 x float>] source, i64 indices 0 and 1 (control, the declared layout)
//   array32: the same with i32 indices
//   vec3:   <3 x float> source, index 1 (12-byte objects 16 bytes apart)
//   struct-*: a struct source, indices 0 then member 1 (nested-vec3: 0, 1, 1); the member
//   after a 3-element vector starts at the vector's allocation size, not its data size:
//     struct-vec3  {float3, float}           member at 16
//     nested-vec3  {float, {float3, float}}  member at 32
//     struct-half3 {half3, half}             member at 8
//     struct-int3  {int3, int}               member at 16
//     struct-lead  {float, float3}           member at 16 (control)
//     struct-vec4  {float4, float}           member at 16 (control)
//     struct-vec2  {float2, float}           member at 8 (control)
int main(int argc, char **argv) try {
    if (argc != 3 || !Iridium::DynamicLLVM::init())
        return 2;
    std::string shape = argv[1];
    auto c = LLVMContextCreate();
    auto m = LLVMModuleCreateWithNameInContext("authored-physical-gep", c);
    auto ptr = LLVMPointerTypeInContext(c, 1);
    auto f32 = LLVMFloatTypeInContext(c);
    auto f16 = LLVMHalfTypeInContext(c);
    auto i32 = LLVMInt32TypeInContext(c);
    auto i64 = LLVMInt64TypeInContext(c);
    auto vec2 = LLVMVectorType(f32, 2);
    auto vec3 = LLVMVectorType(f32, 3);
    auto vec4 = LLVMVectorType(f32, 4);
    auto matrix = LLVMArrayType(vec4, 4);
    auto pair = [&](LLVMTypeRef first, LLVMTypeRef second) {
        LLVMTypeRef members[] = {first, second};
        return LLVMStructTypeInContext(c, members, 2, 0);
    };
    struct StructShape {
        LLVMTypeRef source;
        unsigned depth;
        LLVMTypeRef loaded;
        const char *loadedName;
    };
    std::map<std::string, StructShape> structShapes = {
        {"struct-vec3", {pair(vec3, f32), 1, f32, "float"}},
        {"nested-vec3", {pair(f32, pair(vec3, f32)), 2, f32, "float"}},
        {"struct-half3", {pair(LLVMVectorType(f16, 3), f16), 1, f16, "half"}},
        {"struct-int3", {pair(LLVMVectorType(i32, 3), i32), 1, i32, "int"}},
        {"struct-lead", {pair(f32, vec3), 1, vec3, "float3"}},
        {"struct-vec4", {pair(vec4, f32), 1, f32, "float"}},
        {"struct-vec2", {pair(vec2, f32), 1, f32, "float"}},
    };
    auto structShape = structShapes.find(shape);
    bool isStruct = structShape != structShapes.end();
    if (!isStruct && shape != "vector" && shape != "scalar" && shape != "array" && shape != "array32" && shape != "vec3")
        return 3;
    auto loaded = isStruct ? structShape->second.loaded : shape == "scalar" ? f32 : shape == "vec3" ? vec3 : vec4;
    const char *loadedName = isStruct ? structShape->second.loadedName : shape == "scalar" ? "float" : shape == "vec3" ? "float3" : "float4";
    unsigned alignment = loaded == f16 ? 2 : loaded == f32 || loaded == i32 ? 4 : 16;
    LLVMTypeRef params[] = {ptr, ptr};
    auto function = LLVMAddFunction(m, "authoredKernel", LLVMFunctionType(LLVMVoidTypeInContext(c), params, 2, 0));
    auto writer = LLVMCreateBuilderInContext(c);
    LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
    LLVMValueRef address;
    if (shape == "vector") {
        LLVMValueRef index[] = {LLVMConstInt(i64, 1, 1)};
        address = LLVMBuildGEP2(writer, vec4, LLVMGetParam(function, 0), index, 1, "column");
    } else if (shape == "scalar") {
        LLVMValueRef index[] = {LLVMConstInt(i64, 5, 1)};
        address = LLVMBuildGEP2(writer, f32, LLVMGetParam(function, 0), index, 1, "element");
    } else if (shape == "vec3") {
        LLVMValueRef index[] = {LLVMConstInt(i64, 1, 1)};
        address = LLVMBuildGEP2(writer, vec3, LLVMGetParam(function, 0), index, 1, "object");
    } else if (isStruct) {
        LLVMValueRef index[] = {LLVMConstInt(i64, 0, 1), LLVMConstInt(i32, 1, 0), LLVMConstInt(i32, 1, 0)};
        address = LLVMBuildGEP2(writer, structShape->second.source, LLVMGetParam(function, 0), index, 1 + structShape->second.depth, "member");
    } else {
        auto width = shape == "array32" ? i32 : i64;
        LLVMValueRef index[] = {LLVMConstInt(width, 0, 1), LLVMConstInt(width, 1, 1)};
        address = LLVMBuildGEP2(writer, matrix, LLVMGetParam(function, 0), index, 2, "column");
    }
    auto value = LLVMBuildLoad2(writer, loaded, address, "value");
    LLVMSetAlignment(value, alignment);
    auto store = LLVMBuildStore(writer, value, LLVMGetParam(function, 1));
    LLVMSetAlignment(store, alignment);
    LLVMBuildRetVoid(writer);
    auto str = [&](const char *s) { return LLVMMDStringInContext(c, s, std::strlen(s)); };
    auto n = [&](unsigned x) { return LLVMConstInt(i32, x, 0); };
    LLVMValueRef in[] = {n(0), str("air.buffer"), str("air.location_index"), n(0), n(1), str("air.arg_type_name"), str("float4x4")};
    LLVMValueRef out[] = {n(1), str("air.buffer"), str("air.location_index"), n(1), n(1), str("air.arg_type_name"), str(loadedName)};
    LLVMValueRef descriptors[] = {LLVMMDNodeInContext(c, in, 7), LLVMMDNodeInContext(c, out, 7)};
    LLVMValueRef root[] = {function, LLVMMDNodeInContext(c, nullptr, 0), LLVMMDNodeInContext(c, descriptors, 2)};
    LLVMAddNamedMetadataOperand(m, "air.kernel", LLVMMDNodeInContext(c, root, 3));
    auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);
    Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Kernel, "authoredKernel", LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
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
    FILE *f = std::fopen(argv[2], "wb");
    if (!f)
        return 4;
    std::fwrite(bytes, 1, size, f);
    std::fclose(f);
    std::free(bytes);
    LLVMDisposeMemoryBuffer(bitcode);
    LLVMDisposeBuilder(writer);
    LLVMDisposeModule(m);
    LLVMContextDispose(c);
    std::printf("translated authored %s access\n", shape.c_str());
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
