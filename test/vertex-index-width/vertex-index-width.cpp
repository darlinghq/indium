#include "../../src/iridium/air.cpp"
#include <llvm-c/BitWriter.h>
#include <cstdio>
#include <cstdlib>
int main(int argc,char **argv) try {
 if(argc!=3)return 2;
 if(!Iridium::DynamicLLVM::init())return 3;
 unsigned width=std::strtoul(argv[1],nullptr,10);
 LLVMContextRef context=LLVMContextCreate();
 LLVMModuleRef module=LLVMModuleCreateWithNameInContext("authored-vertex-index",context);
 LLVMTypeRef argument=LLVMIntTypeInContext(context,width);
 LLVMTypeRef functionType=LLVMFunctionType(LLVMVoidTypeInContext(context),&argument,1,0);
 LLVMValueRef function=LLVMAddFunction(module,"authoredVertex",functionType);
 LLVMBuilderRef writer=LLVMCreateBuilderInContext(context);
 LLVMPositionBuilderAtEnd(writer,LLVMAppendBasicBlockInContext(context,function,"entry"));
 if(width<32)LLVMBuildZExt(writer,LLVMGetParam(function,0),LLVMInt32TypeInContext(context),"wide");
 LLVMBuildRetVoid(writer);
 LLVMValueRef fields[]={LLVMConstInt(LLVMInt32TypeInContext(context),0,0),LLVMMDStringInContext(context,"air.vertex_id",13)};
 LLVMValueRef parameter=LLVMMDNodeInContext(context,fields,2);
 LLVMValueRef parameters=LLVMMDNodeInContext(context,&parameter,1);
 LLVMValueRef returns=LLVMMDNodeInContext(context,nullptr,0);
 LLVMValueRef rootFields[]={function,returns,parameters};
 LLVMAddNamedMetadataOperand(module,"air.vertex",LLVMMDNodeInContext(context,rootFields,3));
 LLVMMemoryBufferRef bitcode=LLVMWriteBitcodeToMemoryBuffer(module);
 Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Vertex,"authoredVertex",LLVMGetBufferStart(bitcode),LLVMGetBufferSize(bitcode));
 Iridium::SPIRV::Builder builder;builder.requireCapability(Iridium::SPIRV::Capability::Shader);builder.setAddressingModel(Iridium::SPIRV::AddressingModel::Logical);builder.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);builder.setVersion(1,5);Iridium::OutputInfo info;translated.analyze(builder,info);
 size_t size;void *bytes=builder.finalize(size);FILE *output=std::fopen(argv[2],"wb");if(!output)return 4;
 std::fwrite(bytes,1,size,output);std::fclose(output);std::free(bytes);
 LLVMDisposeMemoryBuffer(bitcode);LLVMDisposeBuilder(writer);LLVMDisposeModule(module);LLVMContextDispose(context);
 std::printf("translated authored %u-bit vertex index\n",width);
} catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
