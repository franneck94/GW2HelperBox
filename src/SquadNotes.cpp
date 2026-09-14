#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>
#include <utility>

#include "SquadNotes.h"
#include "Shared.h"

namespace
{
    std::mutex mutex;
    std::map<std::string, AccountNote> notes;
    std::map<std::string, SquadMember> squad;
    bool notes_dirty = false;

    std::string now_timestamp()
    {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        if (localtime_s(&local, &now) != 0)
            return {};

        char buffer[20] = {};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
        return buffer;
    }

    /* caller must hold the mutex */
    void touch(const SquadMember &member)
    {
        auto &entry = notes[member.account_name];
        const auto timestamp = now_timestamp();

        /* update events fire constantly, so only flag a save when something actually changed */
        const auto character_changed = !member.character_name.empty() && entry.last_character != member.character_name;
        if (entry.last_seen == timestamp && !character_changed)
            return;

        if (character_changed)
            entry.last_character = member.character_name;
        entry.last_seen = timestamp;
        notes_dirty = true;
    }
}

namespace SquadNotes
{
    void Load(const std::filesystem::path &path)
    {
        if (!std::filesystem::exists(path))
            return;

        try
        {
            std::ifstream file(path);
            const auto parsed = json::parse(file);
            if (parsed.is_object())
            {
                std::lock_guard<std::mutex> lock(mutex);
                notes = parsed.get<std::map<std::string, AccountNote>>();
            }
        }
        catch (const std::exception &ex)
        {
            if (Globals::APIDefs != nullptr)
                Globals::APIDefs->Log(ELogLevel_WARNING, Globals::ADDON_NAME,
                                      (std::string("squad_notes.json could not be parsed: ") + ex.what()).c_str());
        }
    }

    void Save(const std::filesystem::path &path)
    {
        json out;
        {
            std::lock_guard<std::mutex> lock(mutex);
            out = notes;
            notes_dirty = false;
        }

        try
        {
            std::ofstream file(path);
            file << out.dump(1, '\t') << std::endl;
        }
        catch (const std::exception &ex)
        {
            if (Globals::APIDefs != nullptr)
                Globals::APIDefs->Log(ELogLevel_WARNING, Globals::ADDON_NAME,
                                      (std::string("squad_notes.json could not be written: ") + ex.what()).c_str());
        }
    }

    void Tick(const std::filesystem::path &path)
    {
        const auto *rtapi = Globals::RTAPIData;
        const auto in_group = rtapi != nullptr && rtapi->GameBuild != 0 &&
                              rtapi->GroupType != RTAPI::EGroupType::None && rtapi->GroupMemberCount > 0;
        if (!in_group)
            ClearSquad();

        auto needs_save = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            needs_save = std::exchange(notes_dirty, false);
        }

        if (needs_save && !path.empty())
            Save(path);
    }

    void MemberJoined(const SquadMember &member)
    {
        if (member.account_name.empty())
            return;

        std::lock_guard<std::mutex> lock(mutex);
        const auto is_new_to_squad = squad.find(member.account_name) == squad.end();
        squad[member.account_name] = member;
        if (is_new_to_squad)
        {
            notes[member.account_name].times_seen += 1;
            notes_dirty = true;
        }
        touch(member);
    }

    void MemberUpdated(const SquadMember &member)
    {
        if (member.account_name.empty())
            return;

        std::lock_guard<std::mutex> lock(mutex);
        squad[member.account_name] = member;
        touch(member);
    }

    void MemberLeft(const std::string &account_name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        squad.erase(account_name);
    }

    void ClearSquad()
    {
        std::lock_guard<std::mutex> lock(mutex);
        squad.clear();
    }

    std::vector<SquadMember> CurrentSquad()
    {
        std::lock_guard<std::mutex> lock(mutex);

        std::vector<SquadMember> members;
        members.reserve(squad.size());
        for (const auto &entry : squad)
            members.push_back(entry.second);

        std::sort(members.begin(), members.end(), [](const auto &lhs, const auto &rhs)
                  {
                      if (lhs.subgroup != rhs.subgroup)
                          return lhs.subgroup < rhs.subgroup;
                      return lhs.account_name < rhs.account_name; });

        return members;
    }

    std::map<std::string, AccountNote> AllNotes()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return notes;
    }

    std::string GetNote(const std::string &account_name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto entry = notes.find(account_name);
        return entry == notes.end() ? std::string{} : entry->second.note;
    }

    void SetNote(const std::string &account_name, const std::string &note)
    {
        if (account_name.empty())
            return;

        std::lock_guard<std::mutex> lock(mutex);
        auto &entry = notes[account_name];
        if (entry.note == note)
            return;

        entry.note = note;
        notes_dirty = true;
    }

    bool AddAccount(const std::string &account_name)
    {
        if (account_name.empty())
            return false;

        std::lock_guard<std::mutex> lock(mutex);
        if (notes.find(account_name) != notes.end())
            return false;

        notes[account_name] = AccountNote{};
        notes_dirty = true;
        return true;
    }

    void RemoveAccount(const std::string &account_name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (notes.erase(account_name) > 0)
            notes_dirty = true;
    }
}
