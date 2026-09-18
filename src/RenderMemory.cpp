#include "Render.h"

#ifdef GW2HB_DEBUG

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
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
#include <type_traits>
#include <vector>

#include "MemoryScanner.h"
#include "Settings.h"
#include "Shared.h"
#include "imgui.h"

namespace
{
    constexpr auto MAX_VISIBLE_READ_SIZE = 4096;
    constexpr auto BYTES_PER_ROW = 16;
    constexpr auto MAX_STRING_BYTES = 128;
    constexpr auto MAX_SCAN_SIZE_MB = 64;
    constexpr auto SCAN_CHUNK_SIZE = 256 * 1024;
    constexpr auto MAX_SEARCH_RESULTS = 100000;
    constexpr auto REFINE_BATCH_SIZE = 512;

    enum class ValueSearchMode
    {
        None,
        Exact,
        Changed,
        Unchanged,
    };

    struct SearchCandidate
    {
        MemoryScanner::Address address = 0;
        std::vector<std::uint8_t> value;
    };

    struct ValueSearchState
    {
        char value[64] = "0";
        int type_index = 4;
        int scan_size_mb = 16;
        bool aligned = true;
        bool running = false;
        ValueSearchMode mode = ValueSearchMode::None;
        MemoryScanner::Address start = 0;
        MemoryScanner::Address cursor = 0;
        MemoryScanner::Address end = 0;
        std::size_t refine_index = 0;
        std::size_t scanned_bytes = 0;
        std::vector<std::uint8_t> needle;
        std::vector<SearchCandidate> candidates;
        std::vector<SearchCandidate> refined_candidates;
        std::string status;
    };

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
        char bookmark_name[64] = {};
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

    void read_memory(MemoryReaderState &state)
    {
        const auto address = resolve_address(state);
        if (!address.has_value())
        {
            state.has_result = false;
            state.status = "Invalid address or offset. Use decimal or hexadecimal beginning with 0x.";
            return;
        }

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

    void navigate_to_address(MemoryReaderState &state, const MemoryScanner::Address address)
    {
        const auto formatted = format_address(address);
        snprintf(state.address, sizeof(state.address), "%s", formatted.c_str());
        snprintf(state.additional_offset, sizeof(state.additional_offset), "0");
        state.executable_relative = false;
        state.selected_offset = 0;
        read_memory(state);
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

    template <typename Value>
    std::optional<std::vector<std::uint8_t>> parse_integral_bytes(const char *text)
    {
        static_assert(std::is_integral_v<Value>);
        char *end = nullptr;
        errno = 0;

        if constexpr (std::is_signed_v<Value>)
        {
            const auto parsed = std::strtoll(text, &end, 0);
            while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)))
                ++end;
            if (errno == ERANGE || end == text || end == nullptr || *end != '\0' ||
                parsed < static_cast<long long>((std::numeric_limits<Value>::min)()) ||
                parsed > static_cast<long long>((std::numeric_limits<Value>::max)()))
                return std::nullopt;

            const auto value = static_cast<Value>(parsed);
            std::vector<std::uint8_t> bytes(sizeof(value));
            std::memcpy(bytes.data(), &value, sizeof(value));
            return bytes;
        }
        else
        {
            if (text[0] == '-')
                return std::nullopt;
            const auto parsed = std::strtoull(text, &end, 0);
            while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)))
                ++end;
            if (errno == ERANGE || end == text || end == nullptr || *end != '\0' ||
                parsed > static_cast<unsigned long long>((std::numeric_limits<Value>::max)()))
                return std::nullopt;

            const auto value = static_cast<Value>(parsed);
            std::vector<std::uint8_t> bytes(sizeof(value));
            std::memcpy(bytes.data(), &value, sizeof(value));
            return bytes;
        }
    }

    template <typename Value>
    std::optional<std::vector<std::uint8_t>> parse_floating_bytes(const char *text)
    {
        char *end = nullptr;
        errno = 0;
        const auto parsed = std::strtod(text, &end);
        while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)))
            ++end;
        if (errno == ERANGE || end == text || end == nullptr || *end != '\0' || !std::isfinite(parsed) ||
            parsed < -(std::numeric_limits<Value>::max)() || parsed > (std::numeric_limits<Value>::max)())
            return std::nullopt;

        const auto value = static_cast<Value>(parsed);
        std::vector<std::uint8_t> bytes(sizeof(value));
        std::memcpy(bytes.data(), &value, sizeof(value));
        return bytes;
    }

    std::optional<std::vector<std::uint8_t>> parse_search_value(const int type_index, const char *text)
    {
        switch (type_index)
        {
        case 0:
            return parse_integral_bytes<std::int8_t>(text);
        case 1:
            return parse_integral_bytes<std::uint8_t>(text);
        case 2:
            return parse_integral_bytes<std::int16_t>(text);
        case 3:
            return parse_integral_bytes<std::uint16_t>(text);
        case 4:
            return parse_integral_bytes<std::int32_t>(text);
        case 5:
            return parse_integral_bytes<std::uint32_t>(text);
        case 6:
            return parse_integral_bytes<std::int64_t>(text);
        case 7:
            return parse_integral_bytes<std::uint64_t>(text);
        case 8:
            return parse_floating_bytes<float>(text);
        case 9:
            return parse_floating_bytes<double>(text);
        default:
            return std::nullopt;
        }
    }

    template <typename Value>
    std::string format_bytes_as(const std::vector<std::uint8_t> &bytes, const bool floating = false)
    {
        if (bytes.size() != sizeof(Value))
            return {};
        Value value{};
        std::memcpy(&value, bytes.data(), sizeof(value));
        if constexpr (std::is_floating_point_v<Value>)
            return floating_string(value);
        else if constexpr (std::is_signed_v<Value>)
            return decimal_string(static_cast<long long>(value));
        else
            return decimal_string(static_cast<unsigned long long>(value));
    }

    std::string format_search_value(const int type_index, const std::vector<std::uint8_t> &bytes)
    {
        switch (type_index)
        {
        case 0:
            return format_bytes_as<std::int8_t>(bytes);
        case 1:
            return format_bytes_as<std::uint8_t>(bytes);
        case 2:
            return format_bytes_as<std::int16_t>(bytes);
        case 3:
            return format_bytes_as<std::uint16_t>(bytes);
        case 4:
            return format_bytes_as<std::int32_t>(bytes);
        case 5:
            return format_bytes_as<std::uint32_t>(bytes);
        case 6:
            return format_bytes_as<std::int64_t>(bytes);
        case 7:
            return format_bytes_as<std::uint64_t>(bytes);
        case 8:
            return format_bytes_as<float>(bytes, true);
        case 9:
            return format_bytes_as<double>(bytes, true);
        default:
            return {};
        }
    }

    bool is_readable_protection(const DWORD protection)
    {
        if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        switch (protection & 0xff)
        {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    void finish_search(ValueSearchState &search, const char *action)
    {
        search.running = false;
        search.mode = ValueSearchMode::None;
        search.status = std::string(action) + " complete: " + std::to_string(search.candidates.size()) + " matches.";
    }

    void advance_exact_search(ValueSearchState &search, const MemoryScanner &scanner)
    {
        if (search.cursor >= search.end)
        {
            finish_search(search, "Exact scan");
            return;
        }

        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void *>(search.cursor), &region, sizeof(region)) == 0)
        {
            search.running = false;
            search.mode = ValueSearchMode::None;
            search.status = "Scan stopped because Windows could not query the next memory region.";
            return;
        }

        const auto region_base = reinterpret_cast<MemoryScanner::Address>(region.BaseAddress);
        if (region.RegionSize > (std::numeric_limits<MemoryScanner::Address>::max)() - region_base)
        {
            search.running = false;
            search.mode = ValueSearchMode::None;
            search.status = "Scan stopped at an invalid memory-region boundary.";
            return;
        }

        const auto region_end = region_base + region.RegionSize;
        const auto segment_end = (std::min)(search.end, region_end);
        if (region.State != MEM_COMMIT || !is_readable_protection(region.Protect))
        {
            search.scanned_bytes += segment_end - search.cursor;
            search.cursor = segment_end;
            return;
        }

        const auto main_size = static_cast<std::size_t>((std::min)(
            static_cast<MemoryScanner::Address>(SCAN_CHUNK_SIZE), segment_end - search.cursor));
        const auto overlap = search.needle.empty() ? 0 : search.needle.size() - 1;
        const auto available = static_cast<std::size_t>(segment_end - search.cursor);
        const auto read_size = (std::min)(available, main_size + overlap);
        const auto result = scanner.Read(search.cursor, read_size);

        if (result)
        {
            const auto alignment = search.aligned ? search.needle.size() : std::size_t{1};
            auto offset = search.aligned
                              ? static_cast<std::size_t>((alignment - (search.cursor % alignment)) % alignment)
                              : std::size_t{0};
            for (; offset < main_size && offset + search.needle.size() <= result.bytes.size(); offset += alignment)
            {
                if (std::equal(search.needle.begin(), search.needle.end(), result.bytes.begin() + offset))
                {
                    search.candidates.push_back(SearchCandidate{search.cursor + offset, search.needle});
                    if (search.candidates.size() >= MAX_SEARCH_RESULTS)
                    {
                        search.running = false;
                        search.mode = ValueSearchMode::None;
                        search.status = "Stopped at 100000 matches. Narrow the range or use aligned values.";
                        return;
                    }
                }
            }
        }

        search.scanned_bytes += main_size;
        search.cursor += main_size;
        if (search.cursor >= search.end)
            finish_search(search, "Exact scan");
    }

    void advance_refine_search(ValueSearchState &search, const MemoryScanner &scanner)
    {
        const auto end = (std::min)(search.candidates.size(), search.refine_index + REFINE_BATCH_SIZE);
        for (; search.refine_index < end; ++search.refine_index)
        {
            const auto &candidate = search.candidates[search.refine_index];
            const auto current = scanner.Read(candidate.address, candidate.value.size());
            if (!current)
                continue;

            const auto changed = current.bytes != candidate.value;
            if ((search.mode == ValueSearchMode::Changed && changed) ||
                (search.mode == ValueSearchMode::Unchanged && !changed))
                search.refined_candidates.push_back(SearchCandidate{candidate.address, current.bytes});
        }

        if (search.refine_index >= search.candidates.size())
        {
            const auto action = search.mode == ValueSearchMode::Changed ? "Changed-value scan" : "Unchanged-value scan";
            search.candidates = std::move(search.refined_candidates);
            finish_search(search, action);
        }
    }

    void advance_value_search(ValueSearchState &search, const MemoryScanner &scanner)
    {
        if (!search.running)
            return;
        if (search.mode == ValueSearchMode::Exact)
            advance_exact_search(search, scanner);
        else
            advance_refine_search(search, scanner);
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

    std::vector<std::uint8_t> null_terminated_bytes(const MemoryScanner::ReadResult &result,
                                                    const std::size_t offset)
    {
        std::vector<std::uint8_t> bytes;
        const auto end = (std::min)(result.bytes.size(), offset + MAX_STRING_BYTES);
        for (auto index = offset; index < end && result.bytes[index] != 0; ++index)
            bytes.push_back(result.bytes[index]);
        return bytes;
    }

    std::string ascii_preview(const MemoryScanner::ReadResult &result, const std::size_t offset)
    {
        const auto bytes = null_terminated_bytes(result, offset);
        if (bytes.empty())
            return "(empty)";

        std::string preview;
        preview.reserve(bytes.size());
        for (const auto byte : bytes)
            preview += std::isprint(static_cast<unsigned char>(byte)) ? static_cast<char>(byte) : '.';
        return preview;
    }

    std::string utf8_preview(const MemoryScanner::ReadResult &result, const std::size_t offset)
    {
        const auto bytes = null_terminated_bytes(result, offset);
        if (bytes.empty())
            return "(empty)";

        const auto *text = reinterpret_cast<const char *>(bytes.data());
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(bytes.size()), nullptr, 0) == 0)
            return "(invalid UTF-8)";

        auto preview = std::string(text, bytes.size());
        for (auto &character : preview)
            if (static_cast<unsigned char>(character) < 0x20)
                character = '.';
        return preview;
    }

    std::string utf16_preview(const MemoryScanner::ReadResult &result, const std::size_t offset)
    {
        if (offset >= result.bytes.size())
            return "(empty)";

        std::wstring wide;
        const auto available = (std::min)(result.bytes.size() - offset, static_cast<std::size_t>(MAX_STRING_BYTES));
        for (std::size_t index = 0; index + sizeof(std::uint16_t) <= available; index += sizeof(std::uint16_t))
        {
            std::uint16_t code_unit = 0;
            std::memcpy(&code_unit, result.bytes.data() + offset + index, sizeof(code_unit));
            if (code_unit == 0)
                break;
            wide.push_back(code_unit < 0x20 ? L'.' : static_cast<wchar_t>(code_unit));
        }

        if (wide.empty())
            return "(empty)";

        const auto output_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                                     static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (output_size <= 0)
            return "(invalid UTF-16)";

        std::string output(static_cast<std::size_t>(output_size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                            output.data(), output_size, nullptr, nullptr);
        return output;
    }

    void render_string_previews(const MemoryReaderState &state)
    {
        const auto offset = static_cast<std::size_t>(state.selected_offset);
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        if (!ImGui::BeginTable("MemoryStringPreviews", 2, flags))
            return;

        ImGui::TableSetupColumn("Encoding", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const auto add_row = [](const char *encoding, const std::string &preview)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(encoding);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", preview.c_str());
        };

        add_row("ASCII", ascii_preview(state.result, offset));
        add_row("UTF-8", utf8_preview(state.result, offset));
        add_row("UTF-16 LE", utf16_preview(state.result, offset));
        ImGui::EndTable();
    }

    void render_bookmarks(MemoryReaderState &state)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (!ImGui::CollapsingHeader("Bookmarks"))
            return;

        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##MemoryBookmarkName", "Label", state.bookmark_name, sizeof(state.bookmark_name));
        ImGui::SameLine();
        if (ImGui::Button("Add selected address"))
        {
            const auto address = state.has_result
                                     ? std::optional<MemoryScanner::Address>{state.result.address + static_cast<std::size_t>(state.selected_offset)}
                                     : resolve_address(state);
            if (!address.has_value())
                state.status = "Enter or select a valid address before adding a bookmark.";
            else
            {
                const auto executable_base = MemoryScanner::ExecutableBaseAddress();
                MEMORY_BASIC_INFORMATION region{};
                const auto in_executable = executable_base != 0 &&
                                           VirtualQuery(reinterpret_cast<const void *>(*address), &region, sizeof(region)) != 0 &&
                                           region.AllocationBase == reinterpret_cast<const void *>(executable_base);
                const auto name = state.bookmark_name[0] != '\0'
                                      ? std::string(state.bookmark_name)
                                      : "Bookmark " + std::to_string(Settings::MemoryBookmarks.size() + 1);
                Settings::MemoryBookmarks.push_back(MemoryBookmark{
                    name,
                    static_cast<std::uint64_t>(in_executable ? *address - executable_base : *address),
                    in_executable,
                    state.read_size,
                });
                Settings::Save(Globals::SettingsPath);
                state.bookmark_name[0] = '\0';
                state.status = "Bookmark saved.";
            }
        }
        render_help("Bookmarks inside Gw2-64.exe are saved as executable-relative offsets so they survive ASLR. Heap addresses remain absolute and usually expire when the game restarts.");

        auto remove_index = -1;
        for (auto index = 0; index < static_cast<int>(Settings::MemoryBookmarks.size()); ++index)
        {
            const auto &bookmark = Settings::MemoryBookmarks[index];
            ImGui::PushID(index);
            if (ImGui::SmallButton("Load"))
            {
                const auto formatted = format_address(static_cast<MemoryScanner::Address>(bookmark.address));
                snprintf(state.address, sizeof(state.address), "%s", formatted.c_str());
                snprintf(state.additional_offset, sizeof(state.additional_offset), "0");
                state.executable_relative = bookmark.executable_relative;
                state.read_size = std::clamp(bookmark.read_size, 1, MAX_VISIBLE_READ_SIZE);
                state.selected_offset = 0;
                read_memory(state);
            }
            ImGui::SameLine();
            ImGui::Text("%s  %s%s", bookmark.name.c_str(),
                        bookmark.executable_relative ? "Gw2-64.exe+" : "",
                        format_address(static_cast<MemoryScanner::Address>(bookmark.address)).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
                remove_index = index;
            ImGui::PopID();
        }

        if (remove_index >= 0)
        {
            Settings::MemoryBookmarks.erase(Settings::MemoryBookmarks.begin() + remove_index);
            Settings::Save(Globals::SettingsPath);
        }
    }

    void render_value_search(MemoryReaderState &reader, ValueSearchState &search)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const auto expanded = ImGui::CollapsingHeader("Value Search");

        if (expanded)
        {
            constexpr const char *types[] = {
                "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float", "double"};
            if (!search.running)
            {
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::Combo("Type##MemorySearch", &search.type_index, types, IM_ARRAYSIZE(types)))
                {
                    search.candidates.clear();
                    search.status.clear();
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputText("Value##MemorySearch", search.value, sizeof(search.value));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::InputInt("MiB##MemorySearch", &search.scan_size_mb, 1, 8);
                search.scan_size_mb = std::clamp(search.scan_size_mb, 1, MAX_SCAN_SIZE_MB);
                ImGui::SameLine();
                ImGui::Checkbox("Aligned", &search.aligned);
            }
            else
                ImGui::Text("%s | value %s | %d MiB | %s", types[search.type_index], search.value,
                            search.scan_size_mb, search.aligned ? "aligned" : "unaligned");
            render_help("The exact scan starts at the resolved reader address and is capped at 64 MiB. Aligned mode checks addresses divisible by the selected value size. Unreadable pages are skipped.");

            if (!search.running)
            {
                if (ImGui::Button("New exact scan"))
                {
                    const auto start = resolve_address(reader);
                    const auto needle = parse_search_value(search.type_index, search.value);
                    const auto scan_size = static_cast<MemoryScanner::Address>(search.scan_size_mb) * 1024 * 1024;
                    if (!start.has_value())
                        search.status = "Enter a valid reader address before scanning.";
                    else if (!needle.has_value())
                        search.status = "The search value is invalid or outside the selected type's range.";
                    else if (scan_size > (std::numeric_limits<MemoryScanner::Address>::max)() - *start)
                        search.status = "The scan range exceeds the address space.";
                    else
                    {
                        search.start = *start;
                        search.cursor = *start;
                        search.end = *start + scan_size;
                        search.scanned_bytes = 0;
                        search.needle = *needle;
                        search.candidates.clear();
                        search.refined_candidates.clear();
                        search.mode = ValueSearchMode::Exact;
                        search.running = true;
                        search.status = "Scanning...";
                    }
                }

                if (!search.candidates.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Keep changed"))
                    {
                        search.mode = ValueSearchMode::Changed;
                        search.refine_index = 0;
                        search.refined_candidates.clear();
                        search.running = true;
                        search.status = "Checking changed values...";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Keep unchanged"))
                    {
                        search.mode = ValueSearchMode::Unchanged;
                        search.refine_index = 0;
                        search.refined_candidates.clear();
                        search.running = true;
                        search.status = "Checking unchanged values...";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear results"))
                    {
                        search.candidates.clear();
                        search.status.clear();
                    }
                }
            }
            else if (ImGui::Button("Cancel scan"))
            {
                search.running = false;
                search.refined_candidates.clear();
                search.mode = ValueSearchMode::None;
                search.status = "Scan cancelled.";
            }

            if (!search.status.empty())
                ImGui::TextUnformatted(search.status.c_str());

            if (search.running)
            {
                const auto progress = search.mode == ValueSearchMode::Exact
                                          ? static_cast<float>(search.cursor - search.start) /
                                                static_cast<float>(search.end - search.start)
                                          : search.candidates.empty()
                                                ? 1.0f
                                                : static_cast<float>(search.refine_index) /
                                                      static_cast<float>(search.candidates.size());
                ImGui::ProgressBar(std::clamp(progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
            }

            if (!search.candidates.empty())
            {
                constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                  ImGuiTableFlags_ScrollY;
                if (ImGui::BeginTable("MemorySearchResults", 2, flags, ImVec2(0.0f, 220.0f)))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 165.0f);
                    ImGui::TableSetupColumn("Current value", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(search.candidates.size()));
                    while (clipper.Step())
                    {
                        for (auto index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                        {
                            const auto &candidate = search.candidates[static_cast<std::size_t>(index)];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::PushID(index);
                            if (ImGui::Selectable(format_address(candidate.address).c_str(), false,
                                                  ImGuiSelectableFlags_SpanAllColumns))
                                navigate_to_address(reader, candidate.address);
                            ImGui::PopID();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(format_search_value(search.type_index, candidate.value).c_str());
                        }
                    }
                    ImGui::EndTable();
                }
            }
        }

        advance_value_search(search, reader.scanner);
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
    static ValueSearchState search;

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
        read_memory(state);

    if (!state.status.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s", state.status.c_str());

    ImGui::Spacing();
    render_bookmarks(state);

    ImGui::Spacing();
    render_value_search(state, search);

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

    ImGui::Spacing();
    ImGui::TextUnformatted("String previews");
    render_help("Previews start at the selected byte, stop at a null terminator, and read at most 128 bytes. UTF-16 uses Windows little-endian code units.");
    render_string_previews(state);
}

#else

void Render::memory_reader_child()
{
}

#endif
