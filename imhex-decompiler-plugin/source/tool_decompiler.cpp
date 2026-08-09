#include "tool_decompiler.hpp"

#include "decompiler.hpp"

#include <hex/api/imhex_api/provider.hpp>
#include <hex/api/imhex_api/hex_editor.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/helpers/logger.hpp>
#include <hex/helpers/fmt.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace hex::plugin::decompiler {

    namespace {

        std::string formatHexByte(u8 byte) {
            char buffer[3];
            std::snprintf(buffer, sizeof(buffer), "%02X", byte);
            return buffer;
        }

        std::string formatBytes(const std::vector<u8> &bytes, size_t maxBytes = 16) {
            std::string result;
            const size_t count = std::min(bytes.size(), maxBytes);
            for (size_t i = 0; i < count; i++) {
                result += formatHexByte(bytes[i]);
                if (i + 1 < count)
                    result += ' ';
            }
            if (bytes.size() > maxBytes)
                result += " ...";
            return result;
        }

        std::string formatAddress(u64 address) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(address));
            return buffer;
        }

        struct ToolState {
            int archIndex = 0;
            int syntaxIndex = 0;
            u64 startAddress = 0;
            u64 size = 0;
            bool sizeIsSet = false;

            std::string startAddressText = "0";
            std::string sizeText;

            std::mutex mutex;
            std::vector<Instruction> instructions;
            bool hasResult = false;
            std::string status;
            u64 disassembledBase = 0;
        };

        ToolState g_state;

        void disassembleSelection() {
            auto provider = hex::ImHexApi::Provider::get();
            if (provider == nullptr) {
                g_state.status = "No file loaded";
                return;
            }

            const auto selection = hex::ImHexApi::HexEditor::getSelection();
            const u64 baseAddress = provider->getBaseAddress();
            const u64 fileSize    = provider->getActualSize();

            u64 start = g_state.startAddress;
            u64 count = g_state.size;

            // When no explicit address is given, fall back to the current selection
            bool addressExplicit = g_state.startAddressText.find_first_not_of(" \t") != std::string::npos &&
                                    g_state.startAddressText != "0";
            if (!addressExplicit && selection.has_value()) {
                start = selection->address;
                if (start < baseAddress || start >= baseAddress + fileSize)
                    start = baseAddress;
            } else {
                if (start < baseAddress)
                    start = baseAddress;
                if (start > baseAddress + fileSize)
                    start = baseAddress + fileSize;
            }

            if (count == 0) {
                const u64 available = (baseAddress + fileSize) - start;
                count = available;
                if (selection.has_value() && selection->address == start && selection->size > 0)
                    count = std::min(selection->size, available);
            }
            count = std::min(count, fileSize - (start - baseAddress));

            if (count == 0) {
                g_state.status = "Nothing to disassemble";
                return;
            }

            const Architecture arch = architectureFromIndex(g_state.archIndex);
            const Syntax syntax     = static_cast<Syntax>(g_state.syntaxIndex);

            // Read the requested data from the provider
            std::vector<u8> buffer(count);
            {
                std::lock_guard lock(g_state.mutex);
                g_state.instructions.clear();
                g_state.hasResult = false;
                g_state.status = "Disassembling...";
            }

            const auto task = hex::TaskManager::createBackgroundTask("Decompiler", [=, buffer = std::move(buffer)](auto &task) mutable {
                task.setMaxValue(count);

                provider->read(start, buffer.data(), buffer.size());

                const auto result = disassemble(arch, syntax, start, buffer.data(), buffer.size());

                {
                    std::lock_guard lock(g_state.mutex);
                    g_state.instructions = std::move(result.instructions);
                    g_state.hasResult    = result.success;
                    g_state.disassembledBase = start;
                    g_state.status = result.success
                        ? fmt::format("{} instructions ({} bytes decoded)", g_state.instructions.size(), result.consumedBytes)
                        : "Error: " + result.error;
                }
            });
        }

        void copyDisassemblyToClipboard() {
            std::lock_guard lock(g_state.mutex);

            std::string output;
            output.reserve(g_state.instructions.size() * 64);
            for (const auto &instruction : g_state.instructions) {
                output += formatAddress(instruction.address);
                output += "  ";
                output += formatBytes(instruction.bytes);
                output += "  ";
                output += instruction.text;
                if (instruction.hasTarget) {
                    output += "    ; -> ";
                    output += formatAddress(instruction.branchTarget);
                }
                output += "\n";
            }

            ImGui::SetClipboardText(output.c_str());
        }

        void drawOptions() {
            if (ImGui::BeginCombo("Architecture", architectureName(architectureFromIndex(g_state.archIndex)))) {
                for (int i = 0; i < architectureCount(); i++) {
                    const bool selected = (g_state.archIndex == i);
                    if (ImGui::Selectable(architectureName(architectureFromIndex(i)), selected))
                        g_state.archIndex = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            const auto arch = architectureFromIndex(g_state.archIndex);
            const bool isX86 = (arch == Architecture::X86_16 || arch == Architecture::X86_32 || arch == Architecture::X86_64);
            if (isX86) {
                if (ImGui::BeginCombo("Syntax", g_state.syntaxIndex == 0 ? "Intel" : "AT&T")) {
                    if (ImGui::Selectable("Intel", g_state.syntaxIndex == 0)) g_state.syntaxIndex = 0;
                    if (ImGui::Selectable("AT&T", g_state.syntaxIndex == 1))  g_state.syntaxIndex = 1;
                    ImGui::EndCombo();
                }
            } else {
                g_state.syntaxIndex = 0;
                ImGui::TextDisabled("Syntax: N/A");
            }

            ImGui::Text("Address range");

            char addressBuffer[32];
            std::snprintf(addressBuffer, sizeof(addressBuffer), "%s", g_state.startAddressText.c_str());
            ImGui::SetNextItemWidth(300);
            if (ImGui::InputText("Start address (hex)", addressBuffer, sizeof(addressBuffer),
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                g_state.startAddressText = addressBuffer;
                if (addressBuffer[0] != '\0')
                    g_state.startAddress = std::strtoull(addressBuffer, nullptr, 16);
                else
                    g_state.startAddress = 0;
            }

            char sizeBuffer[32];
            std::snprintf(sizeBuffer, sizeof(sizeBuffer), "%s", g_state.sizeText.c_str());
            ImGui::SetNextItemWidth(300);
            if (ImGui::InputText("Size (hex, 0 = selection/file)", sizeBuffer, sizeof(sizeBuffer),
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                g_state.sizeText = sizeBuffer;
                g_state.size     = sizeBuffer[0] == '\0' ? 0 : std::strtoull(sizeBuffer, nullptr, 16);
            }
        }

        void drawResultTable() {
            std::lock_guard lock(g_state.mutex);

            if (!g_state.hasResult && g_state.instructions.empty()) {
                ImGui::TextWrapped("%s", g_state.status.c_str());
                return;
            }

            ImGui::Text("%s", g_state.status.c_str());

            if (ImGui::BeginTable("decompiler_disassembly", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable,
                    ImVec2(0.0F, 400.0F))) {
                ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 110.0F);
                ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 170.0F);
                ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 110.0F);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(g_state.instructions.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                        const auto &instruction = g_state.instructions[row];

                        ImGui::TableNextRow();

                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(formatAddress(instruction.address).c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                            if (auto provider = hex::ImHexApi::Provider::get(); provider != nullptr) {
                                hex::ImHexApi::HexEditor::setSelection(instruction.address, instruction.bytes.size(), provider);
                            }
                        }

                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(formatBytes(instruction.bytes).c_str());

                        ImGui::TableNextColumn();
                        if (instruction.isCall)
                            ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.35F, 1.0F), "%s %s", instruction.mnemonic.c_str(), instruction.operands.c_str());
                        else if (instruction.isBranch && instruction.isReturn)
                            ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.35F, 1.0F), "%s %s", instruction.mnemonic.c_str(), instruction.operands.c_str());
                        else if (instruction.isBranch)
                            ImGui::TextColored(ImVec4(0.9F, 0.85F, 0.4F, 1.0F), "%s %s", instruction.mnemonic.c_str(), instruction.operands.c_str());
                        else
                            ImGui::TextUnformatted(instruction.text.c_str());

                        ImGui::TableNextColumn();
                        if (instruction.hasTarget) {
                            ImGui::TextColored(ImVec4(0.5F, 0.8F, 1.0F, 1.0F), "%s", formatAddress(instruction.branchTarget).c_str());
                        }
                    }
                }
                clipper.End();

                ImGui::EndTable();
            }
        }

    }

    void drawDecompilerTool() {
        drawOptions();

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Disassemble")) {
            disassembleSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy to clipboard")) {
            copyDisassemblyToClipboard();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            std::lock_guard lock(g_state.mutex);
            g_state.instructions.clear();
            g_state.hasResult = false;
            g_state.status.clear();
        }

        ImGui::Spacing();
        drawResultTable();
    }

}
