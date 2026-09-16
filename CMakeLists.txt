cmake_minimum_required(VERSION 3.20)
project(GW2HB_Addon)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.80
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(third_party_libs STATIC)
target_include_directories(third_party_libs PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
file(GLOB IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/*.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_sources(third_party_libs PRIVATE ${IMGUI_SOURCES})

add_library(httpclient STATIC
    "httpclient/httpclient.h"
    "httpclient/httpclient.cpp"
)

add_library(nexus INTERFACE
    "nexus/Nexus.h"
)
add_library(mumble INTERFACE
    "mumble/Mumble.h"
)
add_library(rtapi INTERFACE
    "rtapi/RTAPI.hpp"
)

target_link_libraries(third_party_libs PRIVATE
    d3d11
    d3dcompiler
    nexus
    mumble
    rtapi
    httpclient
    nlohmann_json::nlohmann_json
)
target_include_directories(third_party_libs PUBLIC
    "."
)

add_library(first_party_libs STATIC
    "src/Shared.cpp"
    "src/Settings.cpp"
    "src/SquadNotes.cpp"
    "src/Render.cpp"
    "src/RenderUI.cpp"
    "src/Data.cpp"
    "src/KeyboardCapture.cpp"
)
target_include_directories(first_party_libs PUBLIC
    "src"
    "resources"
)
target_link_libraries(first_party_libs PRIVATE
    third_party_libs
)

add_executable(GW2HB_App
    "src/main.cpp"
)
target_link_libraries(GW2HB_App PRIVATE
    third_party_libs
    first_party_libs
)

add_library(GW2HB SHARED
    "src/entry.cpp"
)
target_link_libraries(GW2HB PRIVATE
    third_party_libs
    first_party_libs
)
set_target_properties(GW2HB PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin/debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/release"
)
add_custom_command(TARGET GW2HB POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/merge_squad_notes.ps1"
        "${CMAKE_SOURCE_DIR}/bin/$<LOWER_CASE:$<CONFIG>>/merge_squad_notes.ps1"
    COMMENT "Copying merge_squad_notes.ps1 to bin/$<LOWER_CASE:$<CONFIG>>"
)
add_custom_command(TARGET GW2HB POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>"
        "D:/GW2/addons"
    COMMENT "Copying $<CONFIG> files to GW2 addons directory"
)
