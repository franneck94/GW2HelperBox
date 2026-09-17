#include <windows.h>

#include <shellapi.h>
#include <shlwapi.h>
#include <urlmon.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shlwapi.lib")

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"

#include "httpclient/httpclient.h"
#include "nlohmann/json.hpp"

#include "API.h"
#include "Constants.h"
#include "Data.h"
#include "Render.h"
#include "RenderUI.h"
#include "Settings.h"
#include "Shared.h"

namespace
{
    static const std::string latest_backend_version = "4.4.0";
    static const std::string backend_url =
        "https://github.com/franneck94/Gw2TP/releases/download/" + latest_backend_version + "/GW2TP_Python.exe";

    static const std::string latest_forge_version = "2.0.0";
    static const std::string forge_url =
        "https://github.com/franneck94/GW2MysticForge/releases/download/" + latest_forge_version + "/GW2_Forge.exe";

    static const std::string latest_clicker_version = "2.0.0";
    static const std::string clicker_url =
        "https://github.com/franneck94/GW2_AutoClicker/releases/download/" + latest_clicker_version + "/GW2_AutoClicker.exe";

    std::vector<std::thread> download_threads;

    bool VersionIsLower(const std::string current_version, const std::string &latest_version)
    {
        return current_version < latest_version;
    }

    bool DownloadFile(const std::string &url, const std::filesystem::path &outputPath)
    {
        try
        {
            (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Started Downloading");

            const auto hr = URLDownloadToFileA(nullptr, url.c_str(), outputPath.string().c_str(), 0, nullptr);
            return SUCCEEDED(hr);
        }
        catch (...)
        {
            (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, "Downloading failed.");
            return false;
        }
    }

    bool ExtractZipFile(const std::filesystem::path &zipPath, const std::filesystem::path &extractPath)
    {
        try
        {
            (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Started Extracting");

            const auto psCommand = "powershell.exe -Command \"Expand-Archive -Path '" + zipPath.string() +
                                   "' -DestinationPath '" + extractPath.string() + "' -Force\"";

            STARTUPINFOA si = {sizeof(si)};
            PROCESS_INFORMATION pi = {};
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;

            if (CreateProcessA(nullptr,
                               const_cast<char *>(psCommand.c_str()),
                               nullptr,
                               nullptr,
                               FALSE,
                               0,
                               nullptr,
                               nullptr,
                               &si,
                               &pi))
            {
                (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Started extraction process.");
                WaitForSingleObject(pi.hProcess, 30000); // Wait max 30 seconds
                DWORD exitCode;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                if (exitCode != 0)
                {
                    auto errorMsg = "Extraction failed with exit code: " + std::to_string(exitCode);
                    (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, errorMsg.c_str());
                }

                return exitCode == 0;
            }

            DWORD createProcessError = GetLastError();
            auto errorMsg = "Create Process Failed with error code: " + std::to_string(createProcessError);
            (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, errorMsg.c_str());
            return false;
        }
        catch (...)
        {
            (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, "Extraction failed.");
            return false;
        }
    }

    void DownloadAndExtractDataAsync(const std::filesystem::path &addonPath, const std::string &data_url, const std::string filename)
    {
        download_threads.emplace_back([addonPath, data_url, filename]()
                                      {
            try
            {
                const auto extract_path = addonPath / filename;
                (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Started Download Thread.");

                if (DownloadFile(data_url, extract_path))
                    Settings::Save(Globals::SettingsPath);
            }
            catch (...)
            {
                (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "DownloadAndExtractDataAsync failed.");
            }
        });
    }

    void DropFiles(const std::filesystem::path &path)
    {
        try
        {
            if (std::filesystem::exists(path) && std::filesystem::is_directory(path))
            {
                for (const auto &entry : std::filesystem::directory_iterator(path))
                {
                    if (entry.is_regular_file())
                    {
                        std::filesystem::remove(entry.path());
                    }
                }

                (void)Globals::APIDefs->Log(ELogLevel_INFO,
                                            Globals::ADDON_NAME,
                                            ("Removed old build files from " + path.string() + " directory").c_str());
            }
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            auto errorMsg = "Error removing old builds from " + path.string() + " directory: " + std::string(e.what());
            (void)Globals::APIDefs->Log(ELogLevel_WARNING, Globals::ADDON_NAME, errorMsg.c_str());
        }
        catch (...)
        {
            (void)Globals::APIDefs->Log(
                ELogLevel_WARNING,
                Globals::ADDON_NAME,
                ("Unknown error while removing old builds from " + path.string() + " directory").c_str());
        }
    }

    void center_next_element(const char *label, bool is_text = false)
    {
        const float windowWidth = ImGui::GetWindowContentRegionWidth();
        const float elementWidth = is_text ? ImGui::CalcTextSize(label).x : ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX((windowWidth - elementWidth) * 0.5f);
    }

    void center_group_with_width(const float total_width)
    {
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionWidth() - total_width) * 0.5f);
    }
}

void RenderUI::start_executable(const std::string &exe_path, const std::string &args, bool show_cmd_window)
{
    try
    {
        std::string command = "cmd /k \"" + exe_path + "\"";

        if (!args.empty())
        {
            command += " " + args;
        }

        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = show_cmd_window ? SW_SHOWNORMAL : SW_HIDE;

        if (CreateProcessA(nullptr,
                           const_cast<char *>(command.c_str()),
                           nullptr,
                           nullptr,
                           FALSE,
                           0,
                           nullptr,
                           nullptr,
                           &si,
                           &pi))
        {
            (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, ("Started executable: " + exe_path).c_str());
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else
        {
            DWORD createProcessError = GetLastError();
            auto errorMsg = "Failed to start executable with error code: " + std::to_string(createProcessError);
            (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, errorMsg.c_str());
        }
    }
    catch (...)
    {
        (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, "Executable execution failed.");
    }
}

bool RenderUI::version_is_lower(const std::string current_version, const std::string &latest_version)
{
    return current_version < latest_version;
}

bool RenderUI::download_file(const std::string &url, const std::filesystem::path &outputPath)
{
    try
    {
        (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Started Downloading");

        if (std::filesystem::exists(outputPath))
        {
            (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, ("Deleting old file before download: " + outputPath.string()).c_str());
            std::filesystem::remove(outputPath);
        }

        const auto hr = URLDownloadToFileA(nullptr, url.c_str(), outputPath.string().c_str(), 0, nullptr);
        return SUCCEEDED(hr);
    }
    catch (...)
    {
        (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, "Downloading failed.");
        return false;
    }
}

bool RenderUI::extract_zip_file(const std::filesystem::path &zipPath, const std::filesystem::path &extractPath)
{
    try
    {
        (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Started Extracting");

        const auto psCommand = "powershell.exe -Command \"Expand-Archive -Path '" + zipPath.string() +
                               "' -DestinationPath '" + extractPath.string() + "' -Force\"";

        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (CreateProcessA(nullptr,
                           const_cast<char *>(psCommand.c_str()),
                           nullptr,
                           nullptr,
                           FALSE,
                           0,
                           nullptr,
                           nullptr,
                           &si,
                           &pi))
        {
            (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Started extraction process.");
            WaitForSingleObject(pi.hProcess, 30000);
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            if (exitCode != 0)
            {
                auto errorMsg = "Extraction failed with exit code: " + std::to_string(exitCode);
                (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, errorMsg.c_str());
            }

            return exitCode == 0;
        }

        DWORD createProcessError = GetLastError();
        auto errorMsg = "Create Process Failed with error code: " + std::to_string(createProcessError);
            (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, errorMsg.c_str());
        return false;
    }
    catch (...)
    {
            (void)Globals::APIDefs->Log(ELogLevel_CRITICAL, Globals::ADDON_NAME, "Extraction failed.");
        return false;
    }
}

void RenderUI::download_and_extract_data_async(const std::filesystem::path &addonPath, const std::string &data_url, const std::string filename)
{
    download_threads.emplace_back([addonPath, data_url, filename]()
                                  {
        try
        {
            const auto extract_path = addonPath / filename;
                (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Started Download Thread.");
            const auto download_name = "Downloading: " + data_url + ".";
                (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, download_name.c_str());

            if (RenderUI::download_file(data_url, extract_path))
                Settings::Save(Globals::SettingsPath);
        }
        catch (...)
        {
                (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "DownloadAndExtractDataAsync failed.");
        }
    });
}

void RenderUI::shutdown_download_threads()
{
    for (auto &thread : download_threads)
    {
        if (thread.joinable())
            thread.join();
    }

    download_threads.clear();
}

void RenderUI::update_scripts(const std::filesystem::path &addonPath)
{
    download_and_extract_data_async(addonPath, forge_url, "GW2_Forge.exe");
    download_and_extract_data_async(addonPath, clicker_url, "GW2_AutoClicker.exe");
    download_and_extract_data_async(addonPath, backend_url, "GW2TP_Python.exe");
}

std::string RenderUI::get_clean_category_name(const std::string &input, bool skip_last_two)
{
    auto view = input | std::views::transform(
                            [newWord = true](char c) mutable
                            {
                                if (c == '_')
                                {
                                    newWord = true;
                                    return ' ';
                                }
                                if (newWord)
                                {
                                    newWord = false;
                                    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                                }
                                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            });

    if (skip_last_two)
        return {view.begin(), view.end() - 2};
    else
        return {view.begin(), view.end()};
}

void RenderUI::open_url_in_browser(const std::string &url)
{
    if (!url.empty())
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void RenderUI::render_last_update(char *time_text, size_t buffer_size, const std::chrono::steady_clock::time_point &last_refresh_time)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_refresh_time);

    int total_seconds = static_cast<int>(elapsed.count());
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    if (minutes < 1)
        snprintf(time_text, buffer_size, "Last update: %d seconds ago", seconds);
    else if (hours > 0)
        snprintf(time_text, buffer_size, "Last update: %02d:%02d:%02d ago", hours, minutes, seconds);
    else
        snprintf(time_text, buffer_size, "Last update: %02d:%02d ago", minutes, seconds);
}

void RenderUI::render_table_header(const std::string &name, const std::string &url, bool raw_name)
{
    const auto transformed_name = raw_name ? name : get_clean_category_name(name, false);

    ImGui::TableSetupColumn(transformed_name.c_str(), ImGuiTableColumnFlags_WidthFixed, Render::NAME_COLUMN_WIDTH_PX);
    ImGui::TableSetupColumn("G", ImGuiTableColumnFlags_WidthFixed, Render::NUMBER_COLUMN_WIDTH_PX);
    ImGui::TableSetupColumn("S", ImGuiTableColumnFlags_WidthFixed, Render::NUMBER_COLUMN_WIDTH_PX);
    ImGui::TableSetupColumn("C", ImGuiTableColumnFlags_WidthFixed, Render::NUMBER_COLUMN_WIDTH_PX);
    ImGui::TableHeadersRow();

    if (ImGui::TableGetColumnFlags(0) & ImGuiTableColumnFlags_IsHovered && ImGui::IsMouseDoubleClicked(0))
    {
        if (!url.empty())
            open_url_in_browser(url);
        else
            ImGui::SetClipboardText(transformed_name.c_str());
    }
    if (ImGui::TableGetColumnFlags(1) & ImGuiTableColumnFlags_IsHovered && ImGui::IsMouseDoubleClicked(0))
    {
        if (!url.empty())
            open_url_in_browser(url);
        else
            ImGui::SetClipboardText(transformed_name.c_str());
    }
    if (ImGui::TableGetColumnFlags(2) & ImGuiTableColumnFlags_IsHovered && ImGui::IsMouseDoubleClicked(0))
    {
        if (!url.empty())
            open_url_in_browser(url);
        else
            ImGui::SetClipboardText(transformed_name.c_str());
    }
    if (ImGui::TableGetColumnFlags(3) & ImGuiTableColumnFlags_IsHovered && ImGui::IsMouseDoubleClicked(0))
    {
        if (!url.empty())
            open_url_in_browser(url);
        else
            ImGui::SetClipboardText(transformed_name.c_str());
    }
}

void RenderUI::render_profit_calculator()
{
    static char buy_gold_str[10] = "0";
    static char buy_silver_str[3] = "0";
    static char buy_copper_str[3] = "0";
    static char sell_gold_str[10] = "0";
    static char sell_silver_str[3] = "0";
    static char sell_copper_str[3] = "0";
    static int profit_gold = 0, profit_silver = 0, profit_copper = 0, profit_total = 0;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

    center_next_element("Profit Calculator", true);
    ImGui::TextUnformatted("Profit Calculator");
    ImGui::Spacing();

    const float start_x = ImGui::GetCursorPosX();
    const float window_width = ImGui::GetWindowContentRegionWidth();
    const float total_width = 300.0f;
    ImGui::SetCursorPosX(start_x + (window_width - total_width) * 0.5f);

    ImGui::BeginGroup();
    ImGui::TextUnformatted("Buy Price:");
    ImGui::SameLine();
    ImGui::PushItemWidth(60.0f);
    ImGui::InputText("##buy-g", buy_gold_str, sizeof(buy_gold_str), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextUnformatted("g");
    ImGui::SameLine();
    ImGui::InputText("##buy-s", buy_silver_str, sizeof(buy_silver_str), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextUnformatted("s");
    ImGui::SameLine();
    ImGui::InputText("##buy-c", buy_copper_str, sizeof(buy_copper_str), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextUnformatted("c");
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SetCursorPosX(start_x + (window_width - total_width) * 0.5f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Sell Price:");
    ImGui::SameLine();
    ImGui::PushItemWidth(60.0f);
    ImGui::InputText("##sell-g", sell_gold_str, sizeof(sell_gold_str), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextUnformatted("g");
    ImGui::SameLine();
    ImGui::InputText("##sell-s", sell_silver_str, sizeof(sell_silver_str), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextUnformatted("s");
    ImGui::SameLine();
    ImGui::InputText("##sell-c", sell_copper_str, sizeof(sell_copper_str), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextUnformatted("c");
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    const float child_width = 300.0f;
    ImGui::SetCursorPosX((window_width - child_width) * 0.5f);
    ImGui::BeginChild("calcButtonAndResult", ImVec2(child_width, 50));
    const float button_width = 100.0f;
    if (ImGui::Button("Calculate", ImVec2(button_width, 0)))
    {
        (void)Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Calculating profit...");

        const auto buy_gold = atoi(buy_gold_str);
        const auto buy_silver = atoi(buy_silver_str);
        const auto buy_copper = atoi(buy_copper_str);
        const auto sell_gold = atoi(sell_gold_str);
        const auto sell_silver = atoi(sell_silver_str);
        const auto sell_copper = atoi(sell_copper_str);

        const auto buy_total = (buy_gold * 10000) + (buy_silver * 100) + buy_copper;
        const auto sell_total = (sell_gold * 10000) + (sell_silver * 100) + sell_copper;
        profit_total = static_cast<int>(sell_total * TAX_RATE - buy_total);
        profit_gold = static_cast<int>(profit_total / 10000);
        profit_total %= 10000;
        profit_silver = static_cast<int>(profit_total / 100);
        profit_copper = static_cast<int>(profit_total % 100);
    }

    char profit_text[50];
    snprintf(profit_text, sizeof(profit_text), "Profit: %dg %ds %dc", profit_gold, profit_silver, profit_copper);
    ImGui::SameLine();
    if (profit_total > 0)
        ImGui::TextColored(ImVec4(1.0f, 0.843f, 0.0f, 1.0f), "%s", profit_text);
    else if (profit_total < 0)
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", profit_text);
    else
        ImGui::TextUnformatted(profit_text);

    ImGui::PopStyleVar();
    ImGui::Spacing();
    ImGui::EndChild();
}

void RenderUI::render_krait_materials_calculator()
{
    // Per Krait item (any T5 mat), both share 15 T5 mats + 2 dowels + 1 inscription.
    // Shield: 10 Mithril Ingots, 4 Elder Wood Planks. Trident: 8 Mithril Ingots, 6 Elder Wood Planks.
    // 1 Mithril Ingot = 2 Mithril Ore, 1 Elder Wood Plank = 3 Elder Wood Log.
    const auto render_item = [](const char *title, const char *id, int *num_items, int ingots_per, int planks_per)
    {
        center_next_element(title, true);
        ImGui::TextUnformatted(title);
        ImGui::Spacing();

        const float window_width = ImGui::GetWindowContentRegionWidth();

        const float input_total_width = 200.0f;
        ImGui::SetCursorPosX((window_width - input_total_width) * 0.5f);
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Amount:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt((std::string("##") + id + "-amount").c_str(), num_items, 1, 10);
        ImGui::EndGroup();

        if (*num_items < 0)
            *num_items = 0;

        const int count = *num_items;
        const int t5_mats = 15 * count;
        const int mithril_ingots = ingots_per * count;
        const int elder_wood_planks = planks_per * count;
        const int mithril_ore = 2 * mithril_ingots;
        const int elder_wood_logs = 3 * elder_wood_planks;
        const int dowels = 2 * count;
        const int inscriptions = 1 * count;

        ImGui::Spacing();

        const float table_width = ImGui::GetContentRegionAvail().x;
        if (ImGui::BeginTable((std::string("##") + id + "-table").c_str(), 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(table_width, 0.0f)))
        {
            ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Stacks", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            const auto add_material_row = [](const char *name, int amount)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(name);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", amount);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", amount / 250.0f);
            };

            add_material_row("T5 Materials (any)", t5_mats);
            add_material_row("Mithril Ingots", mithril_ingots);
            add_material_row("Mithril Ore", mithril_ore);
            add_material_row("Elder Wood Planks", elder_wood_planks);
            add_material_row("Elder Wood Logs", elder_wood_logs);
            add_material_row("Dowels", dowels);
            add_material_row("Inscriptions", inscriptions);

            ImGui::EndTable();
        }
    };

    static int num_shields = 1;
    static int num_tridents = 1;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

    const auto available_width = ImGui::GetContentRegionAvail().x;
    const auto child_height = ImGui::GetTextLineHeightWithSpacing() * 11.0f +
                              ImGui::GetStyle().WindowPadding.y * 2.0f;

    ImGui::BeginChild("KraitShieldCalculator", ImVec2(available_width, child_height), false);
    render_item("Krait Shield Materials", "krait-shield", &num_shields, 10, 4);
    ImGui::EndChild();

    ImGui::BeginChild("KraitTridentCalculator", ImVec2(available_width, child_height), false);
    render_item("Krait Trident Materials", "krait-trident", &num_tridents, 8, 6);
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::Spacing();
}

void RenderUI::render_gem_gold_calculator()
{
    using json = nlohmann::json;

    static char gold_input[12] = "100";
    static char gems_input[12] = "400";
    static std::optional<std::future<std::string>> coins_future; // gold -> gems
    static std::optional<std::future<std::string>> gems_future;  // gems -> gold
    static std::string gold_to_gems_result;
    static std::string gems_to_gold_result;

    const auto poll = [](std::optional<std::future<std::string>> &future, std::string &result, bool gems_out)
    {
        if (!future.has_value() || future->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try
        {
            const auto j = json::parse(future->get());
            if (j.contains("quantity"))
            {
                const long long quantity = j.value("quantity", 0LL);
                const long long per_gem = j.value("coins_per_gem", 0LL);
                if (gems_out)
                    result = std::to_string(quantity) + " gems  (" + std::to_string(per_gem) + " copper/gem)";
                else
                {
                    const auto gold = quantity / 10000;
                    const auto silver = (quantity % 10000) / 100;
                    const auto rest = quantity % 100;
                    char gold_per_gem[32];
                    snprintf(gold_per_gem, sizeof(gold_per_gem), "%.4f", per_gem / 10000.0);
                    result = std::to_string(gold) + "g " + std::to_string(silver) + "s " + std::to_string(rest) +
                             "c  (" + gold_per_gem + " gold/gem)";
                }
            }
            else
                result = j.contains("text") ? j["text"].get<std::string>() : "Unexpected response from GW2 API.";
        }
        catch (const std::exception &e)
        {
            result = std::string("Failed to parse response: ") + e.what();
        }
        future.reset();
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

    center_next_element("Gem <-> Gold Exchange", true);
    ImGui::TextUnformatted("Gem <-> Gold Exchange");
    ImGui::Spacing();

    const float window_width = ImGui::GetWindowContentRegionWidth();
    const float total_width = 340.0f;
    const ImVec4 gold_color = ImVec4(1.0f, 0.843f, 0.0f, 1.0f);

    /* gold -> gems */
    ImGui::SetCursorPosX((window_width - total_width) * 0.5f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Gold:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputText("##exchange-gold", gold_input, sizeof(gold_input), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button("Convert to Gems"))
    {
        const long long copper = (std::max)(0LL, atoll(gold_input) * 10000LL);
        const auto url = L"https://api.guildwars2.com/v2/commerce/exchange/coins?quantity=" + std::to_wstring(copper);
        coins_future = HTTPClient::GetRequestAsync(url);
        gold_to_gems_result.clear();
    }
    ImGui::EndGroup();
    if (!gold_to_gems_result.empty())
    {
        ImGui::SetCursorPosX((window_width - total_width) * 0.5f);
        ImGui::TextColored(gold_color, "%s", gold_to_gems_result.c_str());
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* gems -> gold */
    ImGui::SetCursorPosX((window_width - total_width) * 0.5f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Gems:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputText("##exchange-gems", gems_input, sizeof(gems_input), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button("Convert to Gold"))
    {
        const long long gems = (std::max)(0LL, atoll(gems_input));
        const auto url = L"https://api.guildwars2.com/v2/commerce/exchange/gems?quantity=" + std::to_wstring(gems);
        gems_future = HTTPClient::GetRequestAsync(url);
        gems_to_gold_result.clear();
    }
    ImGui::EndGroup();
    if (!gems_to_gold_result.empty())
    {
        ImGui::SetCursorPosX((window_width - total_width) * 0.5f);
        ImGui::TextColored(gold_color, "%s", gems_to_gold_result.c_str());
    }

    poll(coins_future, gold_to_gems_result, true);
    poll(gems_future, gems_to_gold_result, false);

    ImGui::PopStyleVar();
    ImGui::Spacing();
}

void RenderUI::render_top_controls(Data &data, std::chrono::steady_clock::time_point &last_refresh_time)
{
    static auto started_gw2hb_download = false;
    static auto started_forge_download = false;
    static auto started_clicker_download = false;

    bool has_gw2hb_files = false;
    bool has_forge_files = false;
    bool has_clicker_files = false;

    bool is_outdated_gw2hb = false;
    bool is_outdated_forge = false;
    bool is_outdated_clicker = false;

    auto backend_exe = Globals::AddonPath / "GW2TP_Python.exe";
    has_gw2hb_files = std::filesystem::exists(backend_exe);
    auto forge_code_dir = Globals::AddonPath / "GW2_Forge.exe";
    has_forge_files = std::filesystem::exists(forge_code_dir);
    auto clicker_exe = Globals::AddonPath / "GW2_AutoClicker.exe";
    has_clicker_files = std::filesystem::exists(clicker_exe);

    if (has_gw2hb_files && !started_gw2hb_download && VersionIsLower(Settings::BackendVersion, latest_backend_version))
    {
        is_outdated_gw2hb = true;
        (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Outdated backend files detected.");
    }

    if (has_forge_files && !started_forge_download && VersionIsLower(Settings::ForgeVersion, latest_forge_version))
    {
        is_outdated_forge = true;
        (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Outdated forge files detected.");
    }

    if (has_clicker_files && !started_clicker_download && VersionIsLower(Settings::ClickerVersion, latest_clicker_version))
    {
        is_outdated_clicker = true;
        (void)Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Outdated clicker files detected.");
    }

    if ((!has_gw2hb_files || is_outdated_gw2hb) && !started_gw2hb_download)
    {
        started_gw2hb_download = true;
        Settings::BackendVersion = latest_backend_version;
        DownloadAndExtractDataAsync(Globals::AddonPath, backend_url, "GW2TP_Python.exe");
    }

    if ((!has_forge_files || is_outdated_forge) && !started_forge_download)
    {
        started_forge_download = true;
        Settings::ForgeVersion = latest_forge_version;
        DownloadAndExtractDataAsync(Globals::AddonPath, forge_url, "GW2_Forge.exe");
    }

    if ((!has_clicker_files || is_outdated_clicker) && !started_clicker_download)
    {
        started_clicker_download = true;
        Settings::ClickerVersion = latest_clicker_version;
        DownloadAndExtractDataAsync(Globals::AddonPath, clicker_url, "GW2_AutoClicker.exe");
    }

    static int num_forges = 0;
    static int num_clicks = 1;

    const auto *forge_button_label = "Start Forge Script";
    const auto *refresh_button_label = "Refresh Data";
    const auto *loading_label = "Loading...";
    const auto *clicker_button_label = "Start Auto Clicker";
    char time_text[50];
    RenderUI::render_last_update(time_text, sizeof(time_text), last_refresh_time);

    const auto input_width = 100.0f + ImGui::CalcTextSize("Num forges").x + ImGui::GetStyle().ItemInnerSpacing.x;
    const auto clicker_input_width = 60.0f + ImGui::CalcTextSize("Clicks").x + ImGui::GetStyle().ItemInnerSpacing.x;
    const auto forge_btn_width = ImGui::CalcTextSize(forge_button_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const auto refresh_btn_width = ImGui::CalcTextSize(refresh_button_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const auto clicker_btn_width = ImGui::CalcTextSize(clicker_button_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const auto loading_width = ImGui::CalcTextSize(loading_label).x;
    const auto spacing = ImGui::GetStyle().ItemSpacing.x;
    const auto update_text_width = ImGui::CalcTextSize(time_text).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    const auto first_row_width = refresh_btn_width + spacing + update_text_width;
    const auto second_row_width = input_width + spacing + forge_btn_width + spacing + clicker_input_width + spacing + clicker_btn_width;

    center_group_with_width(first_row_width);

    if (data.loaded)
    {
        if (ImGui::Button(refresh_button_label))
        {
            data.loaded = false;
            data.requested = false;
            data.api_data.clear();
            data.futures.clear();
            data.requesting();
            last_refresh_time = std::chrono::steady_clock::now();
        }
    }
    else
    {
        ImGui::TextUnformatted(loading_label);
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(time_text);

    ImGui::Spacing();

    center_group_with_width(second_row_width);

    ImGui::TextUnformatted("Num forges");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("##NumForges", &num_forges, 1, 250);

    ImGui::SameLine();
    if (ImGui::Button(forge_button_label))
    {
        const auto forge_exe_path = (Globals::AddonPath / "GW2_Forge.exe").string();
        const auto forge_args = "-n " + std::to_string(num_forges);

        RenderUI::start_executable(forge_exe_path, forge_args, !Settings::ScriptsInBackground);
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Clicks");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("##Clicks", &num_clicks, 1, 1000);

    ImGui::SameLine();
    if (ImGui::Button(clicker_button_label))
    {
        const auto clicker_exe_path = (Globals::AddonPath / "GW2_AutoClicker.exe").string();
        const auto clicker_args = std::to_string(num_clicks);
        RenderUI::start_executable(clicker_exe_path, clicker_args, !Settings::ScriptsInBackground);
    }
}
