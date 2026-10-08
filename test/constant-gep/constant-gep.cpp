#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) try {
 if((argc!=4 && argc!=5) || !Iridium::DynamicLLVM::init()) return 2;
 bool logical=argc==5 && std::strcmp(argv[4],"logical")==0; if(argc==5 && !logical)return 2;
 unsigned width=std::strtoul(argv[1],nullptr,10); long offset=std::strtol(argv[2],nullptr,10);
 if((width!=32 && width!=64) || offset < -1 || offset > 1) return 3;
 auto context=LLVMContextCreate(); auto module=LLVMModuleCreateWithNameInContext("authored-constant-gep",context);
 auto f32=LLVMFloatTypeInContext(context); auto global=LLVMAddGlobalInAddressSpace(module,f32,"authored_buffer",1);
 auto index=LLVMConstInt(LLVMIntTypeInContext(context,width),offset,1);
 auto expression=LLVMConstGEP2(f32,global,&index,1);
 bool retained=offset?LLVMGetValueKind(expression)==LLVMConstantExprValueKind && LLVMGetConstOpcode(expression)==LLVMGetElementPtr:expression==global;
 if(!retained) return 4;
 if(offset && (LLVMGetIntTypeWidth(LLVMTypeOf(LLVMGetOperand(expression,1)))!=width || ((width==32 || width==64) && LLVMConstIntGetSExtValue(LLVMGetOperand(expression,1))!=offset))) return 6;
 std::printf("authored expression retained width%u offset%ld\n",width,offset);
 using namespace Iridium::SPIRV;
 Builder b; b.setVersion(1,5); b.requireCapability(Capability::Shader); b.requireCapability(Capability::PhysicalStorageBufferAddresses); b.requireCapability(Capability::Int64);
 b.setAddressingModel(AddressingModel::PhysicalStorageBuffer64); b.setMemoryModel(MemoryModel::GLSL450);
 auto scalar=b.declareType(Type(Type::FloatTag{},32));
 auto physical=b.declareType(Type(Type::PointerTag{},StorageClass::PhysicalStorageBuffer,scalar,8));
 auto tableType=b.declareType(Type(Type::StructureTag{},{{physical,0,{}},{physical,8,{}}},16,8));
 b.addDecoration(tableType,{DecorationType::Block,{}});
 auto tablePointer=b.declareType(Type(Type::PointerTag{},StorageClass::Uniform,tableType,8));
 auto table=b.addGlobalVariable(tablePointer,StorageClass::Uniform);
 b.addDecoration(table,{DecorationType::DescriptorSet,{0}}); b.addDecoration(table,{DecorationType::Binding,{0}});
 auto voidType=b.declareType(Type(Type::VoidTag{})); auto functionType=b.declareType(Type(Type::FunctionTag{},voidType,{},8));
 auto function=b.declareFunction(functionType);
 auto size=b.declareConstantScalar<uint32_t>(1);
 b.addEntryPoint({ExecutionModel::GLCompute,function.id,"authoredVertex",{table},{{ExecutionMode::LocalSizeId,true,{size,size,size}}}});
 b.beginFunction(function.id);
 auto pointerPointer=b.declareType(Type(Type::PointerTag{},StorageClass::Uniform,physical,8));
 auto input=b.encodeLoad(physical,b.encodeAccessChain(pointerPointer,table,{b.declareConstantScalar<int32_t>(0)}));
 auto output=b.encodeLoad(physical,b.encodeAccessChain(pointerPointer,table,{b.declareConstantScalar<int32_t>(1)}));
 b.setResultType(input,physical); b.setResultType(output,physical);
 if(logical) {
  auto logicalPointer=b.declareType(Type(Type::PointerTag{},StorageClass::Input,scalar,8));
  input=b.addGlobalVariable(logicalPointer,StorageClass::Input); b.setResultType(input,logicalPointer);
 }
 b.associateExistingResultID(input,reinterpret_cast<uintptr_t>(global));
 ResultID address;
 try { address=llvmValueToResultID(b,expression); }
 catch(const ImpossibleResultID&) { if(logical) { puts("PASS nonphysical constant GEP rejected"); return 0; } throw; }
 if(logical) return 7;
 auto value=b.encodeLoad(scalar,address,4); b.encodeStore(output,value,4);
 b.encodeReturn(); b.endFunction();
 size_t bytesCount; void* bytes=b.finalize(bytesCount); FILE* file=std::fopen(argv[3],"wb"); if(!file)return 5;
 std::fwrite(bytes,1,bytesCount,file); std::fclose(file); std::free(bytes);
 LLVMDisposeModule(module); LLVMContextDispose(context); return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
