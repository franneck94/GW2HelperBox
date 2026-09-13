#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <windows.h>

#include "Data.h"

namespace RenderUI
{
    void start_executable(const std::string &exe_path, const std::string &args, bool show_cmd_window);
    bool version_is_lower(const std::string current_version, const std::string &latest_version);
    bool download_file(const std::string &url, const std::filesystem::path &outputPath);
    bool extract_zip_file(const std::filesystem::path &zipPath, const std::filesystem::path &extractPath);
    void download_and_extract_data_async(const std::filesystem::path &addonPath, const std::string &data_url, const std::string filename);
    void update_scripts(const std::filesystem::path &addonPath);
    void shutdown_download_threads();

    std::string get_clean_category_name(const std::string &input, bool skip_last_two);
    void open_url_in_browser(const std::string &url);

    void render_top_controls(Data &data, std::chrono::steady_clock::time_point &last_refresh_time);
    void render_profit_calculator();
    void render_krait_materials_calculator();
    void render_gem_gold_calculator();
    void render_last_update(char *time_text, size_t buffer_size, const std::chrono::steady_clock::time_point &last_refresh_time);
    void render_table_header(const std::string &name, const std::string &url = "", bool raw_name = false);
}
