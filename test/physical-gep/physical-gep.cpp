#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Target.h>
// Authored kernel: loads from buffer argument 0 through a getelementptr whose source
// element type need not be the buffer's declared pointee, and stores the value to
// argument 1. Each shape names the source type, the constant GEP indices, the loaded
// type and the two arguments' air.arg_type_name; README.md lists them.
//
// On success the fixture prints "expect" and the byte constants the GEP must use, in
// order: the first index's multiplier (the source's allocation size), then per further
// index the struct member offset or the array/vector element stride. They come from
// LLVM's DataLayout under the AIR datalayout, not from Iridium.
static const char *kAIRDataLayout =
    "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-"
    "v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-"
    "v1024:1024:1024-n8:16:32";
int main(int argc, char **argv) try {
    if (argc != 3 || !Iridium::DynamicLLVM::init())
        return 2;
    std::string shapeName = argv[1];
    auto c = LLVMContextCreate();
    auto m = LLVMModuleCreateWithNameInContext("authored-physical-gep", c);
    LLVMSetDataLayout(m, kAIRDataLayout);
    auto td = LLVMCreateTargetData(kAIRDataLayout);
    auto ptr = LLVMPointerTypeInContext(c, 1);
    auto f32 = LLVMFloatTypeInContext(c);
    auto f16 = LLVMHalfTypeInContext(c);
    auto i32 = LLVMInt32TypeInContext(c);
    auto i64 = LLVMInt64TypeInContext(c);
    auto vec2 = LLVMVectorType(f32, 2);
    auto vec3 = LLVMVectorType(f32, 3);
    auto vec4 = LLVMVectorType(f32, 4);
    auto matrix = LLVMArrayType(vec4, 4);
    auto i8 = LLVMInt8TypeInContext(c);
    auto i16 = LLVMInt16TypeInContext(c);
    auto pair = [&](LLVMTypeRef first, LLVMTypeRef second) {
        LLVMTypeRef members[] = {first, second};
        return LLVMStructTypeInContext(c, members, 2, 0);
    };
    LLVMTypeRef packedMembers[] = {f32, vec4, f32};
    auto packedVec4 = LLVMStructTypeInContext(c, packedMembers, 3, 1);
    struct Shape {
        LLVMTypeRef source;
        std::vector<long long> indices;
        LLVMTypeRef loaded;
        const char *outName;
        const char *inName = "float4x4";
        LLVMTypeRef stepIndex = nullptr; // array/vector index type, i64 when null
        bool dynamicFirst = false;       // first index loaded from argument 1, not a constant
    };
    std::map<std::string, Shape> shapes = {
        {"vector", {vec4, {1}, vec4, "float4"}},
        {"scalar", {f32, {5}, f32, "float"}},
        {"array", {matrix, {0, 1}, vec4, "float4"}},
        {"array32", {matrix, {0, 1}, vec4, "float4", "float4x4", i32}},
        {"vec3", {vec3, {1}, vec3, "float3"}},
        {"neg32", {f32, {5}, f32, "float"}},
        {"struct-vec3", {pair(vec3, f32), {0, 1}, f32, "float"}},
        {"nested-vec3", {pair(f32, pair(vec3, f32)), {0, 1, 1}, f32, "float"}},
        {"struct-half3", {pair(LLVMVectorType(f16, 3), f16), {0, 1}, f16, "half"}},
        {"struct-int3", {pair(LLVMVectorType(i32, 3), i32), {0, 1}, i32, "int"}},
        {"struct-lead", {pair(f32, vec3), {0, 1}, vec3, "float3"}},
        {"struct-vec4", {pair(vec4, f32), {0, 1}, f32, "float"}},
        {"struct-vec2", {pair(vec2, f32), {0, 1}, f32, "float"}},
        // a packed_T output name must not change the layout of the vector T
        {"mixed-vec3", {vec3, {1}, vec3, "packed_float3"}},
        {"mixed-half3-member", {pair(f16, LLVMVectorType(f16, 3)), {0, 1}, LLVMVectorType(f16, 3), "packed_half3"}},
        {"mixed-short2-member", {pair(i16, LLVMVectorType(i16, 2)), {0, 1}, LLVMVectorType(i16, 2), "packed_short2"}},
        {"packed2-member", {pair(f32, vec2), {0, 1}, vec2, "packed_float2"}},
        {"packed2-member-int", {pair(i32, LLVMVectorType(i32, 2)), {0, 1}, LLVMVectorType(i32, 2), "packed_int2"}},
        {"packed3-array", {LLVMArrayType(vec3, 2), {0, 1}, vec3, "packed_float3"}},
        {"mixed-uchar3", {LLVMVectorType(i8, 3), {1}, LLVMVectorType(i8, 3), "packed_uchar3"}},
        // controls: LLVM arrays and packed structs keep their LLVM layout
        {"array-member", {pair(f32, LLVMArrayType(f32, 3)), {0, 1, 1}, f32, "float"}},
        {"array-pair", {pair(LLVMArrayType(f32, 4), LLVMArrayType(f32, 2)), {0, 1, 1}, f32, "float"}},
        {"packed-struct-vec4", {packedVec4, {0, 2}, f32, "float"}},
        // vectors wider than 4 lanes are aligned to their power-of-two size
        {"wide-member", {pair(f32, LLVMVectorType(f32, 8)), {0, 1, 0}, f32, "float"}},
        {"wide-member-i8", {pair(i8, LLVMVectorType(i8, 16)), {0, 1, 0}, i8, "char"}},
        // packed_float3 input addressed through a 3-lane vector is refused; other steps are not
        {"guard-packed3", {vec3, {1}, vec3, "float3", "packed_float3"}},
        {"guard-packed3-scalar", {f32, {4}, f32, "float", "packed_float3"}},
        {"guard-packed3-zero", {vec3, {0, 1}, f32, "float", "packed_float3"}},
        {"guard-packed3-dynamic", {vec3, {1}, vec3, "float3", "packed_float3", nullptr, true}},
        {"guard-packed-half3", {LLVMVectorType(f16, 3), {1}, LLVMVectorType(f16, 3), "half3", "packed_half3"}},
        {"guard-packed3-array", {LLVMArrayType(vec3, 2), {0, 1}, vec3, "float3", "packed_float3"}},
        {"guard-packed3-struct", {pair(f32, vec3), {0, 1}, vec3, "float3", "packed_float3"}},
        // a getelementptr with no indices is refused, not read past its operands
        {"guard-packed3-no-index", {vec3, {}, vec3, "float3", "packed_float3"}},
    };
    auto found = shapes.find(shapeName);
    if (found == shapes.end())
        return 3;
    const auto &shape = found->second;
    LLVMTypeRef params[] = {ptr, ptr};
    auto function = LLVMAddFunction(m, "authoredKernel", LLVMFunctionType(LLVMVoidTypeInContext(c), params, 2, 0));
    auto writer = LLVMCreateBuilderInContext(c);
    LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, function, "entry"));
    std::vector<LLVMValueRef> indices;
    std::vector<unsigned long long> expected;
    unsigned long long offset = 0;
    if (shape.dynamicFirst) {
        auto index = LLVMBuildLoad2(writer, i64, LLVMGetParam(function, 1), "index");
        LLVMSetAlignment(index, 8);
        indices.push_back(index);
        expected.push_back(LLVMABISizeOfType(td, shape.source));
    } else if (!shape.indices.empty()) {
        indices.push_back(LLVMConstInt(i64, shape.indices[0], 1));
        expected.push_back(LLVMABISizeOfType(td, shape.source));
        offset = shape.indices[0] * expected[0];
    }
    auto level = shape.source;
    for (size_t i = 1; i < shape.indices.size(); ++i) {
        auto index = shape.indices[i];
        if (LLVMGetTypeKind(level) == LLVMStructTypeKind) {
            indices.push_back(LLVMConstInt(i32, index, 0));
            expected.push_back(LLVMOffsetOfElement(td, level, index));
            offset += expected.back();
            level = LLVMStructGetTypeAtIndex(level, index);
        } else {
            indices.push_back(LLVMConstInt(shape.stepIndex ? shape.stepIndex : i64, index, 1));
            level = LLVMGetElementType(level);
            expected.push_back(LLVMABISizeOfType(td, level));
            offset += index * expected.back();
        }
    }
    auto address = LLVMBuildGEP2(writer, shape.source, LLVMGetParam(function, 0), indices.data(), indices.size(), "address");
    if (shapeName == "neg32") {
        // one element back through an i32 index, which must be sign-extended
        LLVMValueRef back[] = {LLVMConstInt(i32, (unsigned long long)-1, 1)};
        address = LLVMBuildGEP2(writer, f32, address, back, 1, "previous");
        expected.push_back(LLVMABISizeOfType(td, f32));
        offset -= expected.back();
    }
    // An access is only as aligned as its address: the ABI alignment, capped by the
    // largest power of two dividing the byte offset.
    unsigned alignment = LLVMABIAlignmentOfType(td, shape.loaded);
    while (offset % alignment)
        alignment /= 2;
    auto value = LLVMBuildLoad2(writer, shape.loaded, address, "value");
    LLVMSetAlignment(value, alignment);
    auto store = LLVMBuildStore(writer, value, LLVMGetParam(function, 1));
    LLVMSetAlignment(store, LLVMABIAlignmentOfType(td, shape.loaded));
    LLVMBuildRetVoid(writer);
    auto str = [&](const char *s) { return LLVMMDStringInContext(c, s, std::strlen(s)); };
    auto n = [&](unsigned x) { return LLVMConstInt(i32, x, 0); };
    LLVMValueRef in[] = {n(0), str("air.buffer"), str("air.location_index"), n(0), n(1), str("air.arg_type_name"), str(shape.inName)};
    LLVMValueRef out[] = {n(1), str("air.buffer"), str("air.location_index"), n(1), n(1), str("air.arg_type_name"), str(shape.outName)};
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
    LLVMDisposeTargetData(td);
    LLVMContextDispose(c);
    std::printf("expect");
    for (auto constant : expected)
        std::printf(" %llu", constant);
    std::printf("\n");
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
