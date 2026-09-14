#ifndef SQUADNOTES_H
#define SQUADNOTES_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Types.h"

/* Per-account notes, persisted next to settings.json, plus a best-effort view of the
   squad/party the player is currently in (built from the RTAPI group member events). */
namespace SquadNotes
{
    void Load(const std::filesystem::path &path);
    void Save(const std::filesystem::path &path);

    /* Reconciles the tracked squad with RTAPI and flushes pending note changes. Call once per frame. */
    void Tick(const std::filesystem::path &path);

    void MemberJoined(const SquadMember &member);
    void MemberUpdated(const SquadMember &member);
    void MemberLeft(const std::string &account_name);
    void ClearSquad();

    std::vector<SquadMember> CurrentSquad();
    std::map<std::string, AccountNote> AllNotes();
    std::string GetNote(const std::string &account_name);
    void SetNote(const std::string &account_name, const std::string &note);
    bool AddAccount(const std::string &account_name);
    void RemoveAccount(const std::string &account_name);
}

#endif
