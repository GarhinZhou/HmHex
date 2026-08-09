#include "decompiler.hpp"
#include <cstdio>
using namespace hex::plugin::decompiler;
int main() {
    std::printf("step 1: call disassemble\n"); std::fflush(stdout);
    const u8 code[] = { 0xD5, 0x03, 0x20, 0x1F };
    auto r = disassemble(Architecture::AArch64, Syntax::Intel, 0x1000, code, sizeof(code));
    std::printf("step 2: done, success=%d size=%zu\n", r.success, r.instructions.size());
    return 0;
}
