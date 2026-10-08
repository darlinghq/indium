#include <iridium/spirv.hpp>
#include <cstdio>
int main() {
 unsigned failures=0;
 for(bool specializedFirst:{false,true}) {
  Iridium::SPIRV::Builder builder;
  Iridium::SPIRV::ResultID ordinary,specialized;
  if(specializedFirst) {
   specialized=builder.declareConstantScalar<int32_t>(7,17);
   ordinary=builder.declareConstantScalar<int32_t>(7);
  } else {
   ordinary=builder.declareConstantScalar<int32_t>(7);
   specialized=builder.declareConstantScalar<int32_t>(7,17);
  }
  bool independent=ordinary!=specialized;
  bool dedup=ordinary==builder.declareConstantScalar<int32_t>(7);
  bool distinctSpecializations=specialized!=builder.declareConstantScalar<int32_t>(7,18);
  std::printf("%s order=%s independent=%d dedup=%d distinct-specializations=%d\n",independent&&dedup&&distinctSpecializations?"PASS":"FAIL",specializedFirst?"specialized-first":"ordinary-first",independent,dedup,distinctSpecializations);
  failures+=!(independent&&dedup&&distinctSpecializations);
 }
 return failures?1:0;
}
