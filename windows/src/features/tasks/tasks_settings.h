#pragma once

// The tasks page's preferences. The account's key itself is in Credential Manager, never here. No
// Windows headers, so tests run on any host. Matches TasksSettings.swift.

#include <string>
#include <string_view>
#include <vector>

#include "task_model.h"

namespace noitch {

inline constexpr char kDefaultTasksSite[] = "https://scele.cs.ui.ac.id";
// The sites offered in settings, before "Other…": Fasilkom's Scele and UI's EMAS3.
inline constexpr const char* kKnownTaskSites[][2] = {{"Scele \xC2\xB7 scele.cs.ui.ac.id", "https://scele.cs.ui.ac.id"},
                                                     {"EMAS3 \xC2\xB7 emas3.ui.ac.id", "https://emas3.ui.ac.id"}};
inline constexpr int kKnownTaskSiteCount = 2;

// What the page calls the site: "Scele", "EMAS3", or "Moodle" for one typed in.
inline std::string TasksSiteName(std::string_view site) {
    if (site == "https://scele.cs.ui.ac.id") return "Scele";
    if (site == "https://emas3.ui.ac.id") return "EMAS3";
    return "Moodle";
}

// Minutes between checks while the island is closed. 30 is the most often the owner approved
// (2026-11-12); 0 checks only when the island opens, the page shows or the PC wakes.
inline constexpr double kTaskCheckChoices[] = {30, 60, 0};

enum class ReminderChoice { DayAndHour, Hour, Off };

inline std::vector<double> ReminderLeads(ReminderChoice choice) {
    switch (choice) {
        case ReminderChoice::DayAndHour: return {86400, 3600};
        case ReminderChoice::Hour: return {3600};
        default: return {};
    }
}

struct TasksSettings {
    std::string site = kDefaultTasksSite;
    // The signed-in person's name from the site, or empty when signed out.
    std::string accountName;
    double checkMinutes = 30;
    bool alerts = true;
    ReminderChoice reminders = ReminderChoice::DayAndHour;
    TaskScope scope = TaskScope::AssignmentsAndQuizzes;

    bool IsSignedIn() const { return !accountName.empty(); }

    bool operator==(const TasksSettings&) const = default;
};

// "scele.cs.ui.ac.id/" becomes "https://scele.cs.ui.ac.id". Only HTTPS is accepted: the key and, at
// sign-in, the password travel to it. Empty when the text isn't a site.
inline std::string NormalizedTasksSite(std::string_view text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && (text[start] == ' ' || text[start] == '\t')) ++start;
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' || text[end - 1] == '\r')) --end;
    std::string site(text.substr(start, end - start));
    if (site.find("://") == std::string::npos) site = "https://" + site;
    while (!site.empty() && site.back() == '/') site.pop_back();
    constexpr std::string_view scheme = "https://";
    if (site.compare(0, scheme.size(), scheme) != 0) return {};
    const std::string rest = site.substr(scheme.size());
    const std::string host = rest.substr(0, rest.find('/'));
    if (host.find('.') == std::string::npos || host.find(' ') != std::string::npos || host.find('@') != std::string::npos) return {};
    return site;
}

// The host of a normalized site: "scele.cs.ui.ac.id".
inline std::string TasksSiteHost(std::string_view site) {
    constexpr std::string_view scheme = "https://";
    std::string_view rest = site.substr(0, scheme.size()) == scheme ? site.substr(scheme.size()) : site;
    return std::string(rest.substr(0, rest.find('/')));
}

}  // namespace noitch
