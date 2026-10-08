#include "../../src/iridium/air.cpp"
#include <cstdio>
int main() {
    std::string_view base; size_t count;
    splitMetalTypeName("float4x4", base, count);
    bool pass = base == "float" && count == 4;
    std::printf("%s float4x4 base=%.*s first dimension=%zu\n", pass ? "PASS" : "FAIL", (int)base.size(), base.data(), count);
    return pass ? 0 : 1;
}
