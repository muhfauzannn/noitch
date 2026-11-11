#pragma once

// The tasks page's page in the settings window. Matches TasksSettingsPane.swift. The account rows
// (site, username, password, or who is signed in) are one Account row, which the window builds
// because it needs text fields and a request.

#include "features/settings/settings_pane.h"

namespace noitch {

inline SettingsPane TasksSettingsPane() {
    const auto pageOn = [](const AppSettings& s) { return s.pages.showTasks; };
    SettingsRow account;
    account.kind = SettingsRow::Kind::Account;
    account.title = L"Account";
    std::vector<std::pair<std::wstring, double>> checks;
    for (const double minutes : kTaskCheckChoices) {
        checks.emplace_back(minutes == 0 ? L"Only when I open the island" : minutes == 60 ? L"Every hour" : L"Every 30 minutes", minutes);
    }
    return {L"Tasks",
            {
                {L"Account", L"", {account}},
                {L"Checking",
                 L"Moodle can't tell Noitch about new work, so Noitch asks the site. Each check is one small request.",
                 {
                     ChoiceRow(L"Check for new tasks", L"Also when the PC wakes and when the island opens.", checks,
                               [](const AppSettings& s) { return s.tasks.checkMinutes; },
                               [](AppSettings& s, double value) { s.tasks.checkMinutes = value; }, pageOn),
                     ToggleRow(L"Alert on the closed island", L"A line under the notch for a few seconds, then a dot until you've seen the page.",
                               [](const AppSettings& s) { return s.tasks.alerts; }, [](AppSettings& s, bool on) { s.tasks.alerts = on; }, pageOn),
                     ChoiceRow(L"Remind before due", L"", {{L"1 day and 1 hour", 0}, {L"1 hour", 1}, {L"Off", 2}},
                               [](const AppSettings& s) { return static_cast<double>(s.tasks.reminders); },
                               [](AppSettings& s, double value) { s.tasks.reminders = static_cast<ReminderChoice>(static_cast<int>(value)); }, pageOn),
                     ChoiceRow(L"Show", L"", {{L"Assignments and quizzes", 0}, {L"Everything with a due date", 1}},
                               [](const AppSettings& s) { return static_cast<double>(s.tasks.scope); },
                               [](AppSettings& s, double value) { s.tasks.scope = static_cast<TaskScope>(static_cast<int>(value)); }, pageOn),
                 }},
            },
            {}};
}

}  // namespace noitch
