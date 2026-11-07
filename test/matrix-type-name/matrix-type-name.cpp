#include "../../src/iridium/air.cpp"
#include <cstdio>
static unsigned failures;
static void expect(bool value, const char* name) { std::printf("%s %s\n", value ? "PASS" : "FAIL", name); if (!value) ++failures; }
int main() {
    if (!Iridium::DynamicLLVM::init()) return 2;
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = LLVMModuleCreateWithNameInContext("authored-types", context);
    for (const char* scalar : {"float", "half"}) for (unsigned columns = 2; columns <= 4; ++columns) for (unsigned rows = 2; rows <= 4; ++rows) {
        std::string name = std::string(scalar) + std::to_string(columns) + "x" + std::to_string(rows);
        std::string_view base; size_t count, height;
        expect(splitMetalTypeName(name, base, count, &height) && base == scalar && count == columns && height == rows, name.c_str());
        Iridium::SPIRV::Builder builder;
        auto metadata = spirvTypeForAIRTypeName(builder, module, name);
        LLVMTypeRef storage = LLVMArrayType2(LLVMVectorType(std::string(scalar) == "half" ? LLVMHalfTypeInContext(context) : LLVMFloatTypeInContext(context), rows), columns);
        auto llvm = llvmTypeToSPIRVType(builder, storage);
        auto array = builder.reverseLookupType(metadata);
        auto vector = builder.reverseLookupType(array->targetType);
        size_t bytes = std::string(scalar) == "half" ? 2 : 4;
        size_t alignment = (rows == 3 ? 4 : rows) * bytes;
        size_t stride = (rows * bytes + alignment - 1) & ~(alignment - 1);
        expect(metadata == llvm && array->backingType == Iridium::SPIRV::Type::BackingType::Array && array->entryCount == columns && vector->entryCount == rows && array->size == stride * columns && vector->alignment == alignment, (name + " column-array ABI").c_str());
    }
    {
        Iridium::SPIRV::Builder builder;
        LLVMTypeRef storage = LLVMArrayType2(LLVMVectorType(LLVMFloatTypeInContext(context), 3), 4);
        auto first = llvmTypeToSPIRVType(builder, storage);
        auto metadata = spirvTypeForAIRTypeName(builder, module, "float4x3");
        expect(first == metadata && builder.reverseLookupType(first)->size == 64, "LLVM-first padded column-array ABI");
    }
    for (const char* invalid : {"float0", "float5", "float4x0", "float4x4junk", "packed_float4x4", "int4x4"}) {
        bool rejected = false;
        try { Iridium::SPIRV::Builder builder; spirvTypeForAIRTypeName(builder, module, invalid); } catch (const std::runtime_error&) { rejected = true; }
        expect(rejected, invalid);
    }
    std::string_view base; size_t count;
    expect(splitMetalTypeName("packed_float3", base, count) && base == "float" && count == 3, "packed vector control");
    expect(splitMetalTypeName("float", base, count) && base == "float" && count == 1, "scalar control");
    LLVMDisposeModule(module); LLVMContextDispose(context);
    std::printf("RESULT failures=%u\n", failures);
    return failures ? 1 : 0;
}
