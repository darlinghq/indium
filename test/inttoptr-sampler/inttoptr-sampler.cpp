#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
#include <llvm-c/BitWriter.h>
// Authored fragment function that samples a texture through a constant sampler
// referenced as an integer-to-pointer constant expression holding the packed
// sampler state, using the bit layout documented in air.cpp.
int main(int argc, char **argv) try {
    if (argc != 2 || !Iridium::DynamicLLVM::init())
        return 2;
    auto c = LLVMContextCreate();
    auto m = LLVMModuleCreateWithNameInContext("authored-inttoptr-sampler", c);
    auto ptr1 = LLVMPointerTypeInContext(c, 1);
    auto ptr2 = LLVMPointerTypeInContext(c, 2);
    auto f32 = LLVMFloatTypeInContext(c);
    auto i1 = LLVMInt1TypeInContext(c);
    auto i8 = LLVMInt8TypeInContext(c);
    auto i32 = LLVMInt32TypeInContext(c);
    auto i64 = LLVMInt64TypeInContext(c);
    auto vec2 = LLVMVectorType(f32, 2);
    auto ivec2 = LLVMVectorType(i32, 2);
    auto vec4 = LLVMVectorType(f32, 4);
    LLVMTypeRef resultMembers[] = {vec4, i8};
    auto result = LLVMStructTypeInContext(c, resultMembers, 2, 0);
    LLVMTypeRef sampleParams[] = {ptr1, ptr2, vec2, i1, ivec2, i1, f32, i32};
    auto sample = LLVMAddFunction(m, "air.sample_texture_2d.v4f32", LLVMFunctionType(result, sampleParams, 8, 0));
    LLVMTypeRef params[] = {ptr1};
    auto writer = LLVMCreateBuilderInContext(c);
    LLVMValueRef functions[2];
    // s = repeat, t = clamp to edge, r = clamp to edge, mag linear, min nearest,
    // no mip filter, normalized, compare never, anisotropy 1, LOD 0..10.0
    uint64_t packed = 2ull | (1ull << 3) | (1ull << 6) | (1ull << 9) | (0ull << 11) | (0ull << 13) | (8ull << 16)
        | (uint64_t(0x4900) << 40);
    auto sampler = LLVMConstIntToPtr(LLVMConstInt(i64, packed, 0), ptr2);
    auto str = [&](const char *s) { return LLVMMDStringInContext(c, s, std::strlen(s)); };
    auto n = [&](unsigned x) { return LLVMConstInt(i32, x, 0); };
    const char *names[] = {"authoredFragment", "authoredFragment2"};
    for (int i = 0; i < 2; ++i) {
        functions[i] = LLVMAddFunction(m, names[i], LLVMFunctionType(LLVMVoidTypeInContext(c), params, 1, 0));
        LLVMPositionBuilderAtEnd(writer, LLVMAppendBasicBlockInContext(c, functions[i], "entry"));
        LLVMValueRef args[] = {LLVMGetParam(functions[i], 0), sampler, LLVMConstNull(vec2), LLVMConstInt(i1, 0, 0),
                               LLVMConstNull(ivec2), LLVMConstInt(i1, 0, 0), LLVMConstReal(f32, 0), LLVMConstInt(i32, 0, 0)};
        LLVMBuildCall2(writer, LLVMGlobalGetValueType(sample), sample, args, 8, "sampled");
        LLVMBuildRetVoid(writer);
        LLVMValueRef texture[] = {n(0), str("air.texture"), str("air.location_index"), n(0), n(1), str("air.sample"),
                                  str("air.arg_type_name"), str("texture2d<float, sample>")};
        LLVMValueRef descriptors[] = {LLVMMDNodeInContext(c, texture, 8)};
        LLVMValueRef root[] = {functions[i], LLVMMDNodeInContext(c, nullptr, 0), LLVMMDNodeInContext(c, descriptors, 1)};
        LLVMAddNamedMetadataOperand(m, "air.fragment", LLVMMDNodeInContext(c, root, 3));
    }
    auto bitcode = LLVMWriteBitcodeToMemoryBuffer(m);
    Iridium::SPIRV::Builder b;
    b.setVersion(1, 5);
    b.requireCapability(Iridium::SPIRV::Capability::Shader);
    b.requireCapability(Iridium::SPIRV::Capability::PhysicalStorageBufferAddresses);
    b.requireCapability(Iridium::SPIRV::Capability::Int64);
    b.setAddressingModel(Iridium::SPIRV::AddressingModel::PhysicalStorageBuffer64);
    b.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);
    Iridium::OutputInfo info;
    bool ok = true;
    for (auto name : names) {
        Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Fragment, name, LLVMGetBufferStart(bitcode), LLVMGetBufferSize(bitcode));
        translated.analyze(b, info);
        auto &samplers = info.functionInfos[name].embeddedSamplers;
        using S = Iridium::EmbeddedSampler;
        bool matches = samplers.size() == 1 && samplers[0].widthAddressMode == S::AddressMode::Repeat
            && samplers[0].heightAddressMode == S::AddressMode::ClampToEdge && samplers[0].depthAddressMode == S::AddressMode::ClampToEdge
            && samplers[0].magnificationFilter == S::Filter::Linear && samplers[0].minificationFilter == S::Filter::Nearest
            && samplers[0].mipmapFilter == S::MipFilter::None && samplers[0].usesNormalizedCoordinates
            && samplers[0].compareFunction == S::CompareFunction::Never && samplers[0].anisotropyLevel == 1
            && samplers[0].lodMin == 0.0f && samplers[0].lodMax == 10.0f;
        std::printf("%s: %zu embedded sampler(s), state %s\n", name, samplers.size(), matches ? "as authored" : "WRONG");
        ok = ok && matches;
    }
    size_t size;
    void *bytes = b.finalize(size);
    FILE *f = std::fopen(argv[1], "wb");
    if (!f)
        return 4;
    std::fwrite(bytes, 1, size, f);
    std::fclose(f);
    std::free(bytes);
    return ok ? 0 : 5;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
