#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hex::plugin::decompiler {

    using u8  = std::uint8_t;
    using u64 = std::uint64_t;
    using i64 = std::int64_t;

    enum class Architecture : int {
        AArch64 = 0,
        ARM,
        Thumb,
        X86_16,
        X86_32,
        X86_64,
        RISC_V,
    };

    enum class Syntax : int {
        Intel = 0,
        ATT,
    };

    struct Instruction {
        u64 address;          // virtual address the instruction was decoded at
        std::vector<u8> bytes; // raw machine code bytes
        std::string mnemonic;
        std::string operands;
        std::string text;      // mnemonic + operands, pre-formatted

        bool isBranch = false;
        bool isCall    = false;
        bool isReturn  = false;
        bool hasTarget = false;
        u64 branchTarget = 0; // absolute target address when hasTarget is set
    };

    struct DisassemblyResult {
        bool success = false;
        std::string error;
        std::vector<Instruction> instructions;
        size_t consumedBytes = 0;
    };

    /**
     * @brief Disassembles a block of raw machine code using Capstone
     * @param arch        target architecture
     * @param syntax      assembly syntax (Intel/ATT, only affects x86)
     * @param baseAddress address of the first byte (used for absolute branch targets)
     * @param data        raw bytes to disassemble
     * @param size        number of bytes in data
     */
    DisassemblyResult disassemble(Architecture arch, Syntax syntax, u64 baseAddress, const u8 *data, size_t size);

    /**
     * @brief Returns the display name of an architecture
     */
    const char *architectureName(Architecture arch);

    /**
     * @brief Returns the number of available architectures
     */
    int architectureCount();

    /**
     * @brief Maps an index to an architecture
     */
    Architecture architectureFromIndex(int index);

}
