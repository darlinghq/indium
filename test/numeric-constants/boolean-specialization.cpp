#include <iridium/spirv.hpp>
#include <cstdio>
#include <cstdlib>
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 using namespace Iridium::SPIRV;
 Builder builder;builder.setVersion(1,5);builder.requireCapability(Capability::Shader);
 builder.setAddressingModel(AddressingModel::Logical);builder.setMemoryModel(MemoryModel::GLSL450);
 auto specialFalse=builder.declareConstantScalar<bool>(false,17);
 auto ordinaryFalse=builder.declareConstantScalar<bool>(false);
 auto specialTrue=builder.declareConstantScalar<bool>(true,18);
 auto ordinaryTrue=builder.declareConstantScalar<bool>(true);
 if(specialFalse==ordinaryFalse || specialTrue==ordinaryTrue || ordinaryFalse!=builder.declareConstantScalar<bool>(false) || ordinaryTrue!=builder.declareConstantScalar<bool>(true))return 1;
 auto voidType=builder.declareType(Type(Type::VoidTag{}));
 auto functionType=builder.declareType(Type(Type::FunctionTag{},voidType,{},8));
 auto function=builder.declareFunction(functionType);builder.addEntryPoint({ExecutionModel::Vertex,function.id,"authored",{}});
 builder.beginFunction(function.id);builder.encodeReturn();builder.endFunction();
 size_t size;void *bytes=builder.finalize(size);auto words=static_cast<const uint32_t*>(bytes);unsigned found=0;
 for(size_t offset=5;offset<size/4;) {
  unsigned count=words[offset]>>16,opcode=words[offset]&0xffff;if(!count || offset+count>size/4)return 3;
  if(opcode==static_cast<unsigned>(Opcode::ConstantTrue)||opcode==static_cast<unsigned>(Opcode::ConstantFalse)||opcode==static_cast<unsigned>(Opcode::SpecConstantTrue)||opcode==static_cast<unsigned>(Opcode::SpecConstantFalse)){if(count!=3)return 1;++found;}
  offset+=count;
 }
 if(found!=4)return 1;
 FILE *file=std::fopen(argv[1],"wb");if(!file)return 4;std::fwrite(bytes,1,size,file);std::fclose(file);std::free(bytes);
 std::puts("PASS boolean constants, specialization independence and ordinary dedup");
}
