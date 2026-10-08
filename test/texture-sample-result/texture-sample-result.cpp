#include "../../src/iridium/air.cpp"
#include <llvm-c/BitWriter.h>
#include <cstdio>
#include <cstdlib>
int main(int argc,char**argv)try{
 if((argc!=3&&argc!=4)||!Iridium::DynamicLLVM::init())return 2;
 unsigned width=std::strtoul(argv[1],nullptr,10);auto c=LLVMContextCreate();auto m=LLVMModuleCreateWithNameInContext("authored-sample-result",c);
 auto f32=LLVMFloatTypeInContext(c);auto i32=LLVMInt32TypeInContext(c);auto b=LLVMInt1TypeInContext(c);auto v2=LLVMVectorType(f32,2);auto component=argc==4?LLVMHalfTypeInContext(c):f32;auto v4=LLVMVectorType(component,4);auto iv2=LLVMVectorType(i32,2);auto ptr=LLVMPointerTypeInContext(c,0);
 LLVMTypeRef fields[]={v4,width?LLVMIntTypeInContext(c,width):f32};auto result=LLVMStructTypeInContext(c,fields,2,0);
 LLVMTypeRef sampleArgs[]={ptr,ptr,v2,b,iv2,b,f32,f32,i32};auto st=LLVMFunctionType(result,sampleArgs,9,0);auto sample=LLVMAddFunction(m,argc==4?"air.sample_texture_2d.v4f16":"air.sample_texture_2d.v4f32",st);
 LLVMTypeRef args[]={ptr,ptr};auto ft=LLVMFunctionType(LLVMVoidTypeInContext(c),args,2,0);auto function=LLVMAddFunction(m,"authoredFragment",ft);
 auto writer=LLVMCreateBuilderInContext(c);LLVMPositionBuilderAtEnd(writer,LLVMAppendBasicBlockInContext(c,function,"entry"));
 LLVMValueRef callArgs[]={LLVMGetParam(function,0),LLVMGetParam(function,1),LLVMConstNull(v2),LLVMConstNull(b),LLVMConstNull(iv2),LLVMConstNull(b),LLVMConstReal(f32,0),LLVMConstReal(f32,0),LLVMConstNull(i32)};
 auto call=LLVMBuildCall2(writer,st,sample,callArgs,9,"sampled");LLVMBuildExtractValue(writer,call,0,"color");LLVMBuildRetVoid(writer);
 auto str=[&](const char*s){return LLVMMDStringInContext(c,s,std::strlen(s));};auto n=[&](unsigned x){return LLVMConstInt(i32,x,0);};
 LLVMValueRef textureFields[]={n(0),str("air.texture"),str("air.location_index"),n(0),n(1),str("air.sample"),str("air.arg_type_name"),str(argc==4?"texture2d<half, access::sample>":"texture2d<float, access::sample>")};
 LLVMValueRef samplerFields[]={n(1),str("air.sampler"),str("air.location_index"),n(0),n(1)};
 LLVMValueRef params[]={LLVMMDNodeInContext(c,textureFields,8),LLVMMDNodeInContext(c,samplerFields,5)};
 LLVMValueRef root[]={function,LLVMMDNodeInContext(c,nullptr,0),LLVMMDNodeInContext(c,params,2)};LLVMAddNamedMetadataOperand(m,"air.fragment",LLVMMDNodeInContext(c,root,3));
 auto bitcode=LLVMWriteBitcodeToMemoryBuffer(m);Iridium::AIR::Function translated(Iridium::AIR::Function::Type::Fragment,"authoredFragment",LLVMGetBufferStart(bitcode),LLVMGetBufferSize(bitcode));
 Iridium::SPIRV::Builder builder;builder.setVersion(1,5);builder.requireCapability(Iridium::SPIRV::Capability::Shader);builder.setAddressingModel(Iridium::SPIRV::AddressingModel::Logical);builder.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);Iridium::OutputInfo info;translated.analyze(builder,info);
 size_t size;void*bytes=builder.finalize(size);FILE*f=std::fopen(argv[2],"wb");if(!f)return 3;std::fwrite(bytes,1,size,f);std::fclose(f);std::free(bytes);LLVMDisposeMemoryBuffer(bitcode);LLVMDisposeBuilder(writer);LLVMDisposeModule(m);LLVMContextDispose(c);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}
