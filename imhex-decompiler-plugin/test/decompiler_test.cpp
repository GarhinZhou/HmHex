// Standalone unit test for the decompiler core engine.
// Build & run:
//   clang++ -std=c++2b -I source -I deps/prefix/include \
//       test/decompiler_test.cpp source/decompiler.cpp deps/prefix/lib/libcapstone.a \
//       -o /tmp/decompiler_test && /tmp/decompiler_test

#include "decompiler.hpp"

#include <cstdio>
#include <cstring>

using namespace hex::plugin::decompiler;

static int failures = 0;

static void check(bool condition, const char *what) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        failures++;
}

int main() {
    // --- AArch64 (little-endian byte order) ---
    const u8 arm64Code[] = {
        0xE0, 0x03, 0x01, 0xAA, // mov x0, x1
        0x00, 0x04, 0x00, 0x91, // add x0, x0, #1
    };
    // ret = d65f03c0
    const u8 retCode[] = { 0xC0, 0x03, 0x5F, 0xD6 };
    const u8 branchCode[] = {
        0x05, 0x00, 0x00, 0x14, // b 0x14 (offset 0x14 -> target 0x14 from base 0)
    };

    auto r1 = disassemble(Architecture::AArch64, Syntax::Intel, 0x1000, arm64Code, sizeof(arm64Code));
    check(r1.success, "AArch64 disassembly succeeds");
    if (r1.success) {
        check(r1.instructions.size() == 2, "AArch64: 2 instructions");
        check(r1.instructions[0].mnemonic == "mov", "AArch64: first instruction is mov");
        check(r1.instructions[0].bytes.size() == 4, "AArch64: mov is 4 bytes");
        check(r1.instructions[0].address == 0x1000, "AArch64: mov at base address");
        check(r1.instructions[1].mnemonic == "add", "AArch64: second instruction is add");
        check(r1.instructions[1].isBranch == false, "AArch64: add is not a branch");
    }

    auto r2 = disassemble(Architecture::AArch64, Syntax::Intel, 0x2000, retCode, sizeof(retCode));
    check(r2.success && r2.instructions.size() == 1, "AArch64: ret decodes");
    if (r2.success && !r2.instructions.empty()) {
        check(r2.instructions[0].mnemonic == "ret", "AArch64: ret mnemonic");
        check(r2.instructions[0].isReturn, "AArch64: ret is a return");
        check(r2.instructions[0].isBranch, "AArch64: ret is a branch group member");
    }

    auto r3 = disassemble(Architecture::AArch64, Syntax::Intel, 0x0, branchCode, sizeof(branchCode));
    check(r3.success && !r3.instructions.empty(), "AArch64: branch decodes");
    if (r3.success && !r3.instructions.empty()) {
        check(r3.instructions[0].isBranch, "AArch64: b is a branch");
        check(r3.instructions[0].hasTarget, "AArch64: branch has target");
        check(r3.instructions[0].branchTarget == 0x14, "AArch64: branch target resolved");
    }

    // --- x86-64 ---
    const u8 x86Code[] = {
        0x55,             // push rbp
        0x48, 0x89, 0xE5, // mov rbp, rsp
        0xE8, 0x00, 0x00, 0x00, 0x00, // call +0 -> target 0xb (address 0x6 + 5)
        0xC3,             // ret
    };

    auto r4 = disassemble(Architecture::X86_64, Syntax::Intel, 0x0, x86Code, sizeof(x86Code));
    check(r4.success && r4.instructions.size() == 4, "x86-64: 4 instructions");
    if (r4.success && r4.instructions.size() == 4) {
        check(r4.instructions[0].mnemonic == "push", "x86-64: push rbp");
        check(r4.instructions[1].mnemonic == "mov", "x86-64: mov rbp, rsp");
        check(r4.instructions[2].isCall, "x86-64: call detected");
        check(r4.instructions[2].hasTarget, "x86-64: call target resolved");
        // call at address 0x4, size 5, displacement 0 -> target 0x4 + 0x5 = 0x9
        check(r4.instructions[2].branchTarget == 0x9, "x86-64: call target = 0x9");
        check(r4.instructions[3].isReturn, "x86-64: ret detected");
    }

    // --- x86-64 AT&T syntax ---
    auto r5 = disassemble(Architecture::X86_64, Syntax::ATT, 0x0, x86Code, sizeof(x86Code));
    check(r5.success && !r5.instructions.empty(), "x86-64 AT&T syntax works");

    // --- ARM Thumb ---
    const u8 thumbCode[] = { 0x70, 0x47 }; // bx lr (thumb)
    auto r6 = disassemble(Architecture::Thumb, Syntax::Intel, 0x0, thumbCode, sizeof(thumbCode));
    check(r6.success && !r6.instructions.empty(), "Thumb: decodes");
    if (r6.success && !r6.instructions.empty()) {
        check(r6.instructions[0].mnemonic == "bx", "Thumb: bx lr");
        check(r6.instructions[0].isReturn, "Thumb: bx lr is return");
    }

    // --- ARM ---
    const u8 armCode[] = { 0x1E, 0xFF, 0x2F, 0xE1 }; // bx lr (arm)
    auto r7 = disassemble(Architecture::ARM, Syntax::Intel, 0x0, armCode, sizeof(armCode));
    check(r7.success && !r7.instructions.empty(), "ARM: decodes");
    if (r7.success && !r7.instructions.empty()) {
        check(r7.instructions[0].mnemonic == "bx", "ARM: bx lr");
        check(r7.instructions[0].isReturn, "ARM: bx lr is return");
    }

    // --- undecodable garbage ---
    const u8 garbage[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    auto r8 = disassemble(Architecture::AArch64, Syntax::Intel, 0x0, garbage, sizeof(garbage));
    check(r8.success, "Garbage: engine does not fail");
    check(r8.instructions.size() == 5, "Garbage: 5 placeholder instructions");

    // --- RISC-V (if supported by build) ---
    const u8 riscvCode[] = { 0x13, 0x00, 0x00, 0x00 }; // nop
    auto r9 = disassemble(Architecture::RISC_V, Syntax::Intel, 0x0, riscvCode, sizeof(riscvCode));
    check(r9.success, "RISC-V: decodes");

    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
