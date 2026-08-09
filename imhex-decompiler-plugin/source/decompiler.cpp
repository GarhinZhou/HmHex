#include "decompiler.hpp"

#include <capstone/capstone.h>

#include <cstring>
#include <optional>
#include <string_view>

namespace hex::plugin::decompiler {

    int architectureCount() {
        return 7;
    }

    Architecture architectureFromIndex(int index) {
        switch (index) {
            case 0: return Architecture::AArch64;
            case 1: return Architecture::ARM;
            case 2: return Architecture::Thumb;
            case 3: return Architecture::X86_16;
            case 4: return Architecture::X86_32;
            case 5: return Architecture::X86_64;
            default: return Architecture::RISC_V;
        }
    }

    const char *architectureName(Architecture arch) {
        switch (arch) {
            case Architecture::AArch64: return "AArch64 (ARM64)";
            case Architecture::ARM:     return "ARM (32-bit)";
            case Architecture::Thumb:   return "ARM Thumb";
            case Architecture::X86_16:  return "x86 (16-bit)";
            case Architecture::X86_32:  return "x86 (32-bit)";
            case Architecture::X86_64:  return "x86-64";
            case Architecture::RISC_V:  return "RISC-V (64-bit)";
        }
        return "Unknown";
    }

    namespace {

        struct ArchMapping {
            cs_arch arch;
            cs_mode mode;
        };

        std::optional<ArchMapping> toCapstone(Architecture arch) {
            switch (arch) {
                case Architecture::AArch64: return ArchMapping { CS_ARCH_ARM64, CS_MODE_ARM };
                case Architecture::ARM:     return ArchMapping { CS_ARCH_ARM,   CS_MODE_ARM };
                case Architecture::Thumb:   return ArchMapping { CS_ARCH_ARM,   CS_MODE_THUMB };
                case Architecture::X86_16:  return ArchMapping { CS_ARCH_X86,   CS_MODE_16 };
                case Architecture::X86_32:  return ArchMapping { CS_ARCH_X86,   CS_MODE_32 };
                case Architecture::X86_64:  return ArchMapping { CS_ARCH_X86,   CS_MODE_64 };
                case Architecture::RISC_V:  return ArchMapping { CS_ARCH_RISCV, static_cast<cs_mode>(CS_MODE_RISCV64) };
            }
            return std::nullopt;
        }

        bool isBranching(const cs_insn *insn) {
            if (insn->detail == nullptr)
                return false;

            for (u8 i = 0; i < insn->detail->groups_count; i++) {
                const auto group = insn->detail->groups[i];
                if (group == CS_GRP_JUMP || group == CS_GRP_CALL || group == CS_GRP_RET || group == CS_GRP_IRET)
                    return true;
            }
            return false;
        }

        bool isCall(const cs_insn *insn) {
            if (insn->detail == nullptr)
                return false;

            for (u8 i = 0; i < insn->detail->groups_count; i++) {
                if (insn->detail->groups[i] == CS_GRP_CALL)
                    return true;
            }
            return false;
        }

        bool isReturn(const cs_insn *insn) {
            if (insn->detail != nullptr) {
                for (u8 i = 0; i < insn->detail->groups_count; i++) {
                    if (insn->detail->groups[i] == CS_GRP_RET || insn->detail->groups[i] == CS_GRP_IRET)
                        return true;
                }
            }

            // ARM/Thumb "bx lr" / "b lr" return idioms are not tagged with CS_GRP_RET by Capstone
            const std::string_view mnemonic(insn->mnemonic);
            if (mnemonic == "bx" || mnemonic == "b") {
                const std::string_view operands(insn->op_str);
                return operands.find("lr") != std::string_view::npos;
            }

            return false;
        }

        // Extracts an immediate target from the instruction detail.
        // On x86 the relative immediate must be resolved to an absolute address.
        std::optional<u64> getBranchTarget(const cs_insn *insn, Architecture arch) {
            if (insn->detail == nullptr)
                return std::nullopt;

            switch (arch) {
                case Architecture::AArch64: {
                    for (u8 i = 0; i < insn->detail->arm64.op_count; i++) {
                        const auto &op = insn->detail->arm64.operands[i];
                        if (op.type == ARM64_OP_IMM)
                            return op.imm;
                    }
                    break;
                }
                case Architecture::ARM:
                case Architecture::Thumb: {
                    for (u8 i = 0; i < insn->detail->arm.op_count; i++) {
                        const auto &op = insn->detail->arm.operands[i];
                        if (op.type == ARM_OP_IMM)
                            return op.imm;
                    }
                    break;
                }
                case Architecture::X86_16:
                case Architecture::X86_32:
                case Architecture::X86_64: {
                    for (u8 i = 0; i < insn->detail->x86.op_count; i++) {
                        const auto &op = insn->detail->x86.operands[i];
                        if (op.type == X86_OP_IMM) {
                            // Capstone 5.x already resolves PC-relative branch targets to
                            // absolute addresses in op.imm
                            return op.imm;
                        }
                    }
                    break;
                }
                case Architecture::RISC_V: {
                    for (u8 i = 0; i < insn->detail->riscv.op_count; i++) {
                        const auto &op = insn->detail->riscv.operands[i];
                        if (op.type == RISCV_OP_IMM)
                            return op.imm;
                    }
                    break;
                }
            }

            return std::nullopt;
        }

    }

    DisassemblyResult disassemble(Architecture arch, Syntax syntax, u64 baseAddress, const u8 *data, size_t size) {
        DisassemblyResult result;

        const auto mapping = toCapstone(arch);
        if (!mapping.has_value()) {
            result.error = "Unsupported architecture";
            return result;
        }

        csh handle;
        if (cs_open(mapping->arch, mapping->mode, &handle) != CS_ERR_OK) {
            result.error = "Failed to initialize disassembly engine";
            return result;
        }

        // Enable instruction detail so branch/call/return groups are available
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
        if ((arch == Architecture::X86_16 || arch == Architecture::X86_32 || arch == Architecture::X86_64) &&
            syntax == Syntax::ATT) {
            cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
        }

        const u8 *codePtr = data;
        size_t remaining  = size;
        u64 currentAddr   = baseAddress;

        cs_insn *insn = cs_malloc(handle);

        while (remaining > 0) {
            if (!cs_disasm_iter(handle, &codePtr, &remaining, &currentAddr, insn)) {
                // Undecodable byte: emit a placeholder instruction and skip one byte
                result.instructions.push_back(Instruction {
                    currentAddr,
                    { *codePtr },
                    "??",
                    "",
                    "??",
                    false, false, false, false, 0
                });
                codePtr += 1;
                remaining -= 1;
                currentAddr += 1;
                continue;
            }

            Instruction decoded;
            decoded.address  = insn->address;
            decoded.bytes.assign(insn->bytes, insn->bytes + insn->size);
            decoded.mnemonic = insn->mnemonic;
            decoded.operands = insn->op_str;
            decoded.text     = std::string(insn->mnemonic) + " " + insn->op_str;
            decoded.isCall   = isCall(insn);
            decoded.isReturn = isReturn(insn);
            decoded.isBranch = isBranching(insn);

            if (decoded.isBranch && !decoded.isReturn) {
                if (const auto target = getBranchTarget(insn, arch); target.has_value()) {
                    decoded.hasTarget    = true;
                    decoded.branchTarget = *target;
                }
            }

            result.instructions.push_back(std::move(decoded));
        }

        cs_free(insn, 1);

        result.consumedBytes = size - remaining;
        result.success = true;
        cs_close(&handle);

        return result;
    }

}
