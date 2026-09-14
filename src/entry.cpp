#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <DirectXMath.h>
#include <Windows.h>

#include "imgui.h"
#include "httpclient/httpclient.h"
#include "mumble/Mumble.h"
#include "nexus/Nexus.h"
#include "rtapi/RTAPI.hpp"

#include "GW2HB_Hover_data.h"
#include "GW2HB_Normal_data.h"
#include "KeyboardCapture.h"
#include "Render.h"
#include "RenderUI.h"
#include "Settings.h"
#include "Shared.h"
#include "SquadNotes.h"
#include "Version.h"

namespace dx = DirectX;

void AddonLoad(AddonAPI *aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();
void OnRTAPIGroupMemberJoined(void *aEventArgs);
void OnRTAPIGroupMemberLeft(void *aEventArgs);
void OnRTAPIGroupMemberUpdated(void *aEventArgs);

HMODULE hSelf;
AddonDefinition AddonDef{};
Render render{Settings::ShowWindow};
bool RTAPIGroupEventSubscribed = false;

void SubscribeRTAPIGroupEvent()
{
    if (!RTAPIGroupEventSubscribed && Globals::APIDefs != nullptr)
    {
        Globals::APIDefs->SubscribeEvent(EV_RTAPI_GROUP_MEMBER_JOINED, static_cast<EVENT_CONSUME>(OnRTAPIGroupMemberJoined));
        Globals::APIDefs->SubscribeEvent(EV_RTAPI_GROUP_MEMBER_LEFT, static_cast<EVENT_CONSUME>(OnRTAPIGroupMemberLeft));
        Globals::APIDefs->SubscribeEvent(EV_RTAPI_GROUP_MEMBER_UPDATED, static_cast<EVENT_CONSUME>(OnRTAPIGroupMemberUpdated));
        RTAPIGroupEventSubscribed = true;
        Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Subscribed to RTAPI group member events.");
    }
}

void UnsubscribeRTAPIGroupEvent()
{
    if (RTAPIGroupEventSubscribed && Globals::APIDefs != nullptr)
    {
        Globals::APIDefs->UnsubscribeEvent(EV_RTAPI_GROUP_MEMBER_JOINED, static_cast<EVENT_CONSUME>(OnRTAPIGroupMemberJoined));
        Globals::APIDefs->UnsubscribeEvent(EV_RTAPI_GROUP_MEMBER_LEFT, static_cast<EVENT_CONSUME>(OnRTAPIGroupMemberLeft));
        Globals::APIDefs->UnsubscribeEvent(EV_RTAPI_GROUP_MEMBER_UPDATED, static_cast<EVENT_CONSUME>(OnRTAPIGroupMemberUpdated));
        RTAPIGroupEventSubscribed = false;
    }
}

void ToggleShowWindowGW2HB(const char *keybindIdentifier)
{
    Settings::ToggleShowWindow(Globals::SettingsPath);
}

void RegisterQuickAccessShortcut()
{
    Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Registering GW2HB quick access shortcut");
    Globals::APIDefs->AddShortcut("SHORTCUT_GW2HB", "TEX_GW2HB_NORMAL", "TEX_GW2HB_HOVER", Globals::KB_TOGGLE_GW2HB, "Toggle GW2 HelperBox++ Window");
}

void DeregisterQuickAccessShortcut()
{
    Globals::APIDefs->Log(ELogLevel_DEBUG, Globals::ADDON_NAME, "Deregistering GW2HB quick access shortcut");
    Globals::APIDefs->RemoveShortcut("SHORTCUT_GW2HB");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        hSelf = hModule;
        break;
    case DLL_PROCESS_DETACH:
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition *GetAddonDef()
{
    AddonDef.Signature = -1245534;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "GW2HB";
    AddonDef.Version.Major = MAJOR;
    AddonDef.Version.Minor = MINOR;
    AddonDef.Version.Build = BUILD;
    AddonDef.Version.Revision = REVISION;
    AddonDef.Author = "Franneck.1274";
    AddonDef.Description = "GW2 HelperBox++ - Guild Wars 2 information tools.";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = EAddonFlags_None;
    AddonDef.Provider = EUpdateProvider_GitHub;
    AddonDef.UpdateLink = "https://github.com/franneck94/GW2HelperBox/releases/latest";

    return &AddonDef;
}

void OnRTAPILoadedEvent(void *aEventArgs)
{
    const auto *signature = static_cast<const int *>(aEventArgs);
    if (!signature || *signature != RTAPI_SIG)
        return;

    Globals::RTAPIData = (RTAPI::RealTimeData *)Globals::APIDefs->GetResource(DL_RTAPI);
    SubscribeRTAPIGroupEvent();
    Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "RTAPI loaded after GW2HB; group member join events should now be delivered.");
}
void OnRTAPIUnloadedEvent(void *aEventArgs)
{
    const auto *signature = static_cast<const int *>(aEventArgs);
    if (!signature || *signature != RTAPI_SIG)
        return;

    UnsubscribeRTAPIGroupEvent();
    Globals::RTAPIData = nullptr;
}

namespace
{
    SquadMember ToSquadMember(const RTAPI::GroupMember *member)
    {
        const auto read_name = [](const char *name, size_t capacity)
        {
            return std::string(name, strnlen_s(name, capacity));
        };

        return SquadMember{
            read_name(member->AccountName, sizeof(member->AccountName)),
            read_name(member->CharacterName, sizeof(member->CharacterName)),
            static_cast<int>(member->Subgroup),
            member->IsSelf != 0,
            member->IsCommander != 0,
            member->IsLieutenant != 0,
            member->IsInInstance != 0,
        };
    }

    const char *CurrentGroupLabel()
    {
        if (Globals::RTAPIData == nullptr)
            return "group";

        switch (Globals::RTAPIData->GroupType)
        {
        case RTAPI::EGroupType::Party:
            return "party";
        case RTAPI::EGroupType::RaidSquad:
        case RTAPI::EGroupType::Squad:
            return "squad";
        default:
            return "group";
        }
    }
}

void OnRTAPIGroupMemberJoined(void *aEventArgs)
{
    if (aEventArgs == nullptr || Globals::APIDefs == nullptr)
        return;

    const auto member = ToSquadMember(static_cast<RTAPI::GroupMember *>(aEventArgs));
    SquadNotes::MemberJoined(member);

    std::string message = std::string("Joined ") + CurrentGroupLabel() + ": " +
                          (member.account_name.empty() ? "<unknown>" : member.account_name);
    if (!member.character_name.empty())
        message += " (" + member.character_name + ")";
    Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, message.c_str());
}

void OnRTAPIGroupMemberLeft(void *aEventArgs)
{
    if (aEventArgs == nullptr || Globals::APIDefs == nullptr)
        return;

    const auto member = ToSquadMember(static_cast<RTAPI::GroupMember *>(aEventArgs));
    SquadNotes::MemberLeft(member.account_name);
}

void OnRTAPIGroupMemberUpdated(void *aEventArgs)
{
    if (aEventArgs == nullptr || Globals::APIDefs == nullptr)
        return;

    SquadNotes::MemberUpdated(ToSquadMember(static_cast<RTAPI::GroupMember *>(aEventArgs)));
}

void AddonLoad(AddonAPI *aApi)
{
    Globals::APIDefs = aApi;
    ImGui::SetCurrentContext((ImGuiContext *)Globals::APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions((void *(*)(size_t, void *))Globals::APIDefs->ImguiMalloc, (void (*)(void *, void *))Globals::APIDefs->ImguiFree); // on imgui 1.80+

    Globals::NexusLink = (NexusLinkData *)Globals::APIDefs->GetResource("DL_NEXUS_LINK");
    Globals::RTAPIData = (RTAPI::RealTimeData *)Globals::APIDefs->GetResource(DL_RTAPI);
    if (Globals::RTAPIData != nullptr)
        Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "RTAPI already loaded at GW2HB startup; group member join events will be received.");
    else
        Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "RTAPI not loaded yet at GW2HB startup; group member join events arrive only if RTAPI is (re)loaded after GW2HB.");
    Globals::APIDefs->SubscribeEvent("EV_ADDON_LOADED", OnRTAPILoadedEvent);
    Globals::APIDefs->SubscribeEvent("EV_ADDON_UNLOADED", OnRTAPIUnloadedEvent);
    SubscribeRTAPIGroupEvent();
    Globals::APIDefs->RegisterRender(ERenderType_Render, AddonRender);
    Globals::APIDefs->RegisterRender(ERenderType_OptionsRender, AddonOptions);
    Globals::AddonPath = Globals::APIDefs->GetAddonDirectory(Globals::ADDON_NAME);
    Globals::SettingsPath = Globals::APIDefs->GetAddonDirectory("GW2HB/settings.json");
    Globals::SquadNotesPath = Globals::APIDefs->GetAddonDirectory("GW2HB/squad_notes.json");
    std::filesystem::create_directory(Globals::AddonPath);
    Settings::Load(Globals::SettingsPath);
    SquadNotes::Load(Globals::SquadNotesPath);
    Settings::ShowWindow = false;

    // Initialize KeyboardCapture
    KeyboardCapture::GetInstance().Initialize(
        Globals::APIDefs->RegisterWndProc,
        Globals::APIDefs->DeregisterWndProc);

    Globals::APIDefs->LoadTextureFromMemory("TEX_GW2HB_NORMAL",
                                            (void *)GW2HB_NORMAL,
                                            GW2HB_NORMAL_size,
                                            nullptr);
    Globals::APIDefs->LoadTextureFromMemory("TEX_GW2HB_HOVER",
                                            (void *)GW2HB_HOVER,
                                            GW2HB_HOVER_size,
                                            nullptr);
    Globals::APIDefs->RegisterKeybindWithString(Globals::KB_TOGGLE_GW2HB, ToggleShowWindowGW2HB, "(null)");
    RegisterQuickAccessShortcut();
}

void AddonUnload()
{
    Globals::APIDefs->DeregisterRender(AddonOptions);
    Globals::APIDefs->DeregisterRender(AddonRender);

    RenderUI::shutdown_download_threads();
    render.data.shutdown();
    HTTPClient::Shutdown();

    UnsubscribeRTAPIGroupEvent();
    Globals::APIDefs->UnsubscribeEvent("EV_ADDON_LOADED", OnRTAPILoadedEvent);
    Globals::APIDefs->UnsubscribeEvent("EV_ADDON_UNLOADED", OnRTAPIUnloadedEvent);

    Globals::NexusLink = nullptr;
    Globals::RTAPIData = nullptr;

    KeyboardCapture::GetInstance().Shutdown();

    if (Globals::ForgeProcessActive && Globals::ForgeProcessInfo.hProcess)
    {
        TerminateProcess(Globals::ForgeProcessInfo.hProcess, 0);
        CloseHandle(Globals::ForgeProcessInfo.hProcess);
        CloseHandle(Globals::ForgeProcessInfo.hThread);
        Globals::ForgeProcessActive = false;
        Globals::ForgeProcessInfo = {};
    }

    if (Globals::AutoClickerProcessActive && Globals::AutoClickerProcessInfo.hProcess)
    {
        TerminateProcess(Globals::AutoClickerProcessInfo.hProcess, 0);
        CloseHandle(Globals::AutoClickerProcessInfo.hProcess);
        CloseHandle(Globals::AutoClickerProcessInfo.hThread);
        Globals::AutoClickerProcessActive = false;
        Globals::AutoClickerProcessInfo = {};
    }

    if (Globals::GW2TPServerProcessActive && Globals::GW2TPServerProcessInfo.hProcess)
    {
        TerminateProcess(Globals::GW2TPServerProcessInfo.hProcess, 0);
        CloseHandle(Globals::GW2TPServerProcessInfo.hProcess);
        CloseHandle(Globals::GW2TPServerProcessInfo.hThread);
        Globals::GW2TPServerProcessActive = false;
        Globals::GW2TPServerProcessInfo = {};
    }

    Settings::Save(Globals::SettingsPath);
    SquadNotes::Save(Globals::SquadNotesPath);

    DeregisterQuickAccessShortcut();
    Globals::APIDefs->DeregisterKeybind(Globals::KB_TOGGLE_GW2HB);
}

void AddonRender()
{
    if ((!Globals::NexusLink) || (!Globals::NexusLink->IsGameplay))
        return;

    SquadNotes::Tick(Globals::SquadNotesPath);

    if (Settings::ShowWindow)
    {
        auto &keyboard = KeyboardCapture::GetInstance();

        if (keyboard.WasKeyPressed(VK_ESCAPE))
        {
            Settings::ShowWindow = false;
            Settings::Save(Globals::SettingsPath);
            return;
        }

        if (keyboard.IsKeyDown(VK_CONTROL) && keyboard.WasKeyPressed('C'))
        {
            if (Globals::ForgeProcessInfo.hProcess)
            {
                Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Interrupting forge python script...");
                GenerateConsoleCtrlEvent(CTRL_C_EVENT, Globals::ForgeProcessInfo.dwProcessId);
                Sleep(500); // Give it a moment to gracefully exit
                TerminateProcess(Globals::ForgeProcessInfo.hProcess, 0);
                CloseHandle(Globals::ForgeProcessInfo.hProcess);
                CloseHandle(Globals::ForgeProcessInfo.hThread);
                Globals::ForgeProcessActive = false;
                Globals::ForgeProcessInfo = {};
            }

            if (Globals::AutoClickerProcessInfo.hProcess)
            {
                Globals::APIDefs->Log(ELogLevel_INFO, Globals::ADDON_NAME, "Interrupting auto-clicker process...");
                GenerateConsoleCtrlEvent(CTRL_C_EVENT, Globals::AutoClickerProcessInfo.dwProcessId);
                Sleep(500);
                TerminateProcess(Globals::AutoClickerProcessInfo.hProcess, 0);
                CloseHandle(Globals::AutoClickerProcessInfo.hProcess);
                CloseHandle(Globals::AutoClickerProcessInfo.hThread);
                Globals::AutoClickerProcessActive = false;
                Globals::AutoClickerProcessInfo = {};
            }
        }

        render.data.requesting();
        render.data.storing();
    }

    /* runs even while the main window is hidden, so outdated order popups keep working */
    render.render();
}

void AddonOptions()
{
}
