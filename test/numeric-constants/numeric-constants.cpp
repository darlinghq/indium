#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char **argv) {
 if(argc!=2 || !Iridium::DynamicLLVM::init())return 2;
 LLVMContextRef context=LLVMContextCreate();unsigned failures=0;
 for(unsigned width:{1u,8u,16u,32u,64u}) {
  uint64_t sign=uint64_t{1}<<(width-1);
  std::vector<uint64_t> values=width==1?std::vector<uint64_t>{0,1}:std::vector<uint64_t>{0,~uint64_t{0},sign,sign-1};
  for(size_t index=0;index<values.size();++index) {
   Iridium::SPIRV::Builder builder;
   builder.setVersion(1,5);builder.requireCapability(Iridium::SPIRV::Capability::Shader);
   builder.setAddressingModel(Iridium::SPIRV::AddressingModel::Logical);builder.setMemoryModel(Iridium::SPIRV::MemoryModel::GLSL450);
   auto value=LLVMConstInt(LLVMIntTypeInContext(context,width),values[index],0);
   auto translated=llvmValueToResultID(builder,value);
   auto expected=llvmTypeToSPIRVType(builder,LLVMIntTypeInContext(context,width));
   auto vector=builder.declareType(Iridium::SPIRV::Type(Iridium::SPIRV::Type::VectorTag{},2,expected,width==1?2:width/4,width==1?2:width/4));
   builder.declareConstantComposite(vector,{translated,translated});
   auto voidType=builder.declareType(Iridium::SPIRV::Type(Iridium::SPIRV::Type::VoidTag{}));
   auto functionType=builder.declareType(Iridium::SPIRV::Type(Iridium::SPIRV::Type::FunctionTag{},voidType,{},8));
   auto function=builder.declareFunction(functionType);
   builder.addEntryPoint({Iridium::SPIRV::ExecutionModel::Vertex,function.id,"authored",{}});
   builder.beginFunction(function.id);
   if(width==1) {
    int labels[3];auto yes=builder.associateResultID(reinterpret_cast<uintptr_t>(&labels[0]));auto no=builder.associateResultID(reinterpret_cast<uintptr_t>(&labels[1]));auto merge=builder.associateResultID(reinterpret_cast<uintptr_t>(&labels[2]));
    builder.encodeSelectionMerge(merge);builder.encodeBranchConditional(translated,yes,no);
    builder.insertLabel(yes);builder.encodeBranch(merge);builder.insertLabel(no);builder.encodeBranch(merge);builder.insertLabel(merge);
   } else builder.encodeArithBinop(Iridium::SPIRV::Opcode::IAdd,expected,translated,translated);
   builder.encodeReturn();builder.endFunction();
   size_t size;void *bytes=builder.finalize(size);
   std::string path=std::string(argv[1])+"-"+std::to_string(width)+"-"+std::to_string(index)+".spv";
   auto words=static_cast<const uint32_t*>(bytes);bool preserved=false;
   for(size_t offset=5;offset<size/4;) {
    unsigned count=words[offset]>>16,opcode=words[offset]&0xffff;if(!count || offset+count>size/4)return 4;
    if(count>=3 && words[offset+2]==translated) {
     if(opcode==static_cast<unsigned>(Iridium::SPIRV::Opcode::Constant)) {
      uint64_t literal=words[offset+3];if(count==5)literal|=uint64_t{words[offset+4]}<<32;
      uint64_t mask=width==64?~uint64_t{0}:((uint64_t{1}<<width)-1);
      preserved=(literal&mask)==LLVMConstIntGetZExtValue(value);
     } else if(opcode==static_cast<unsigned>(Iridium::SPIRV::Opcode::ConstantTrue))preserved=width==1 && values[index]==1;
     else if(opcode==static_cast<unsigned>(Iridium::SPIRV::Opcode::ConstantFalse))preserved=width==1 && values[index]==0;
    }
    offset+=count;
   }
   std::printf("%s constant bit pattern width=%u case=%zu\n",preserved?"PASS":"FAIL",width,index);failures+=!preserved;
   FILE *file=std::fopen(path.c_str(),"wb");if(!file)return 3;
   std::fwrite(bytes,1,size,file);std::fclose(file);std::free(bytes);
   std::printf("authored width=%u case=%zu bits=%llx %s\n",width,index,(unsigned long long)LLVMConstIntGetZExtValue(value),path.c_str());
  }
 }
 for(unsigned width:{3u,128u}) {
  Iridium::SPIRV::Builder builder;bool rejected=false;
  try {llvmValueToResultID(builder,LLVMConstInt(LLVMIntTypeInContext(context,width),1,0));}
  catch(const ImpossibleResultID&){rejected=true;}
  std::printf("%s unsupported width=%u\n",rejected?"PASS":"FAIL",width);failures+=!rejected;
 }
 LLVMContextDispose(context);return failures?1:0;
}
