#include "Render.h"

#ifdef GW2HB_DEBUG

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "MemoryScanner.h"
#include "imgui.h"

namespace
{
    constexpr auto MAX_VISIBLE_READ_SIZE = 4096;
    constexpr auto BYTES_PER_ROW = 16;

    struct MemoryReaderState
    {
        MemoryScanner scanner;
        MemoryScanner::ReadResult result;
        char address[32] = "0x0";
        char additional_offset[32] = "0";
        int read_size = 256;
        int selected_offset = 0;
        bool executable_relative = false;
        bool continuous_refresh = false;
        bool has_result = false;
        std::string status;
    };

    void render_help(const char *text)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    std::string format_address(const MemoryScanner::Address address)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setfill('0')
               << std::setw(sizeof(MemoryScanner::Address) * 2) << address;
        return stream.str();
    }

    bool apply_offset(const MemoryScanner::Address base, std::string_view text,
                      MemoryScanner::Address &address)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
        {
            address = base;
            return true;
        }

        const auto last = text.find_last_not_of(" \t\r\n");
        text = text.substr(first, last - first + 1);

        auto subtract = false;
        if (text.front() == '+' || text.front() == '-')
        {
            subtract = text.front() == '-';
            text.remove_prefix(1);
        }

        const auto offset = MemoryScanner::ParseAddress(text);
        if (!offset.has_value())
            return false;

        if (subtract)
        {
            if (*offset > base)
                return false;
            address = base - *offset;
        }
        else
        {
            if (*offset > (std::numeric_limits<MemoryScanner::Address>::max)() - base)
                return false;
            address = base + *offset;
        }

        return true;
    }

    std::optional<MemoryScanner::Address> resolve_address(const MemoryReaderState &state)
    {
        const auto entered = MemoryScanner::ParseAddress(state.address);
        if (!entered.has_value())
            return std::nullopt;

        auto base = *entered;
        if (state.executable_relative)
        {
            const auto executable_base = MemoryScanner::ExecutableBaseAddress();
            if (*entered > (std::numeric_limits<MemoryScanner::Address>::max)() - executable_base)
                return std::nullopt;
            base = executable_base + *entered;
        }

        MemoryScanner::Address resolved = 0;
        return apply_offset(base, state.additional_offset, resolved)
                   ? std::optional<MemoryScanner::Address>{resolved}
                   : std::nullopt;
    }

    template <typename Value>
    std::optional<Value> value_at(const MemoryScanner::ReadResult &result, const std::size_t offset)
    {
        if (offset > result.bytes.size() || sizeof(Value) > result.bytes.size() - offset)
            return std::nullopt;

        Value value{};
        std::memcpy(&value, result.bytes.data() + offset, sizeof(Value));
        return value;
    }

    template <typename Value>
    std::string decimal_string(const Value value)
    {
        return std::to_string(value);
    }

    template <typename Value>
    std::string floating_string(const Value value)
    {
        std::ostringstream stream;
        stream << std::setprecision(std::numeric_limits<Value>::max_digits10) << value;
        return stream.str();
    }

    void render_interpretations(const MemoryReaderState &state)
    {
        const auto offset = static_cast<std::size_t>(state.selected_offset);
        const auto selected_address = state.result.address + offset;
        ImGui::Text("Selected address: %s", format_address(selected_address).c_str());
        render_help("Windows on x64 stores these multi-byte values in little-endian order. The first selected byte is the least significant byte.");

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        if (!ImGui::BeginTable("MemoryInterpretations", 3, flags))
            return;

        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableHeadersRow();

        const auto add_row = [](const char *type, const std::string &value, const int bytes)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(type);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", bytes);
        };

        if (const auto value = value_at<std::int8_t>(state.result, offset))
            add_row("int8", decimal_string(static_cast<int>(*value)), sizeof(*value));
        if (const auto value = value_at<std::uint8_t>(state.result, offset))
            add_row("uint8", decimal_string(static_cast<unsigned int>(*value)), sizeof(*value));
        if (const auto value = value_at<std::int16_t>(state.result, offset))
            add_row("int16", decimal_string(*value), sizeof(*value));
        if (const auto value = value_at<std::uint16_t>(state.result, offset))
            add_row("uint16", decimal_string(*value), sizeof(*value));
        if (const auto value = value_at<std::int32_t>(state.result, offset))
            add_row("int32", decimal_string(*value), sizeof(*value));
        if (const auto value = value_at<std::uint32_t>(state.result, offset))
            add_row("uint32", decimal_string(*value), sizeof(*value));
        if (const auto value = value_at<std::int64_t>(state.result, offset))
            add_row("int64", decimal_string(*value), sizeof(*value));
        if (const auto value = value_at<std::uint64_t>(state.result, offset))
            add_row("uint64", decimal_string(*value), sizeof(*value));
        if (const auto value = value_at<float>(state.result, offset))
            add_row("float", floating_string(*value), sizeof(*value));
        if (const auto value = value_at<double>(state.result, offset))
            add_row("double", floating_string(*value), sizeof(*value));
        if (const auto value = value_at<MemoryScanner::Address>(state.result, offset))
            add_row("pointer", format_address(*value), sizeof(*value));

        ImGui::EndTable();
    }

    void render_hex_dump(MemoryReaderState &state)
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("MemoryHexDump", BYTES_PER_ROW + 2, flags, ImVec2(0.0f, 320.0f)))
            return;

        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 145.0f);
        for (auto column = 0; column < BYTES_PER_ROW; ++column)
        {
            char label[3]{};
            snprintf(label, sizeof(label), "%02X", column);
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, 27.0f);
        }
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        for (std::size_t row_start = 0; row_start < state.result.bytes.size(); row_start += BYTES_PER_ROW)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_address(state.result.address + row_start).c_str());

            std::string ascii;
            ascii.reserve(BYTES_PER_ROW);
            for (auto column = 0; column < BYTES_PER_ROW; ++column)
            {
                ImGui::TableNextColumn();
                const auto byte_offset = row_start + static_cast<std::size_t>(column);
                if (byte_offset >= state.result.bytes.size())
                    continue;

                const auto byte = state.result.bytes[byte_offset];
                char label[24]{};
                snprintf(label, sizeof(label), "%02X##byte_%zu", byte, byte_offset);
                const auto selected = state.selected_offset == static_cast<int>(byte_offset);
                if (ImGui::Selectable(label, selected))
                    state.selected_offset = static_cast<int>(byte_offset);

                ascii += std::isprint(static_cast<unsigned char>(byte)) ? static_cast<char>(byte) : '.';
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ascii.c_str());
        }

        ImGui::EndTable();
    }
}

void Render::memory_reader_child()
{
    static MemoryReaderState state;

    const auto executable_base = MemoryScanner::ExecutableBaseAddress();
    ImGui::Text("Executable base: %s", format_address(executable_base).c_str());
    render_help("When loaded by Nexus, the executable is Gw2-64.exe. Its base changes between launches because of ASLR, so module-relative offsets are usually easier to reuse.");

    if (ImGui::RadioButton("Absolute address", !state.executable_relative))
        state.executable_relative = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Executable + offset", state.executable_relative))
        state.executable_relative = true;

    ImGui::TextUnformatted(state.executable_relative ? "Executable offset" : "Address");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(210.0f);
    ImGui::InputText("##MemoryAddress", state.address, sizeof(state.address));
    render_help(state.executable_relative
                    ? "Enter an offset from the executable base, such as 0x1234. The reader adds it to the base shown above."
                    : "Enter an absolute virtual address, such as 0x00007FF612341000. Decimal addresses also work.");

    ImGui::TextUnformatted("Additional offset");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(210.0f);
    ImGui::InputText("##MemoryAdditionalOffset", state.additional_offset, sizeof(state.additional_offset));
    render_help("This is added after the address is resolved. Both +0x20 and -16 are valid. Use 0 when no extra offset is needed.");

    ImGui::TextUnformatted("Bytes to read");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("##MemoryReadSize", &state.read_size, 16, 256);
    state.read_size = std::clamp(state.read_size, 1, MAX_VISIBLE_READ_SIZE);
    render_help("A larger range gives more context but is harder to inspect. Start with 64 or 256 bytes. This view is capped at 4096 bytes.");

    auto read_requested = ImGui::Button("Read memory");
    ImGui::SameLine();
    ImGui::Checkbox("Continuous refresh", &state.continuous_refresh);
    render_help("Refreshes every frame. Leave this off while editing an address. Memory can change while the game runs.");
    read_requested = read_requested || state.continuous_refresh;

    if (read_requested)
    {
        const auto address = resolve_address(state);
        if (!address.has_value())
        {
            state.has_result = false;
            state.status = "Invalid address or offset. Use decimal or hexadecimal beginning with 0x.";
        }
        else
        {
            state.result = state.scanner.Read(*address, static_cast<std::size_t>(state.read_size));
            state.has_result = static_cast<bool>(state.result);
            if (state.has_result)
            {
                state.status.clear();
                state.selected_offset = std::clamp(state.selected_offset, 0, state.read_size - 1);
            }
            else
            {
                const auto message = std::system_category().message(state.result.error_code);
                state.status = "Read failed (Windows error " + std::to_string(state.result.error_code) + "): " + message;
            }
        }
    }

    if (!state.status.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s", state.status.c_str());

    if (!state.has_result)
        return;

    ImGui::Spacing();
    ImGui::Text("Read %zu bytes from %s", state.result.bytes.size(), format_address(state.result.address).c_str());
    render_hex_dump(state);

    ImGui::Spacing();
    ImGui::TextUnformatted("Interpret from byte");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("##MemorySelectedOffset", &state.selected_offset);
    state.selected_offset = std::clamp(state.selected_offset, 0, static_cast<int>(state.result.bytes.size()) - 1);
    render_help("Click a byte in the hex table or enter its zero-based position. Each value below starts at that byte.");
    render_interpretations(state);
}

#else

void Render::memory_reader_child()
{
}

#endif
