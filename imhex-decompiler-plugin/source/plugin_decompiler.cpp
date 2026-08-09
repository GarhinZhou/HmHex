#include <hex/plugin.hpp>

#include <hex/api/content_registry/tools.hpp>
#include <hex/helpers/logger.hpp>

#include "decompiler.hpp"
#include "tool_decompiler.hpp"

using namespace hex;

IMHEX_PLUGIN_SETUP(
    "Decompiler",
    "AICoding",
    "Multi-architecture disassembler / decompiler powered by Capstone. "
    "Disassembles the selected region of the currently open file into assembly "
    "for AArch64, ARM, Thumb, x86 (16/32/64-bit) and RISC-V.")
{
    hex::ContentRegistry::Tools::add("Decompiler", "", &hex::plugin::decompiler::drawDecompilerTool);
}
