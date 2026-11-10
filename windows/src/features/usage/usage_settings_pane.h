#pragma once

// The usage page's page in the settings window. Matches UsageSettingsPane.swift.

#include "features/settings/settings_pane.h"

namespace noitch {

inline SettingsPane UsageSettingsPane() {
    std::vector<std::pair<std::wstring, double>> levels;
    for (const double level : kWarnLevels) levels.emplace_back(std::to_wstring(static_cast<int>(level)) + L"%", level);
    std::vector<std::pair<std::wstring, double>> refresh;
    for (const double minutes : kRefreshChoices) {
        refresh.emplace_back(minutes == 1 ? L"Minute" : std::to_wstring(static_cast<int>(minutes)) + L" minutes", minutes);
    }
    const auto pageOn = [](const AppSettings& s) { return s.pages.showUsage; };
    return {L"AI Usage",
            {
                {L"Tools",
                 L"Claude Code's limits are asked of Anthropic with its own login. Codex's come from its local logs.",
                 {
                     ToggleRow(L"Claude Code", L"", [](const AppSettings& s) { return s.usage.showClaude; },
                               [](AppSettings& s, bool on) { s.usage.showClaude = on; }, pageOn),
                     ToggleRow(L"Codex", L"", [](const AppSettings& s) { return s.usage.showCodex; },
                               [](AppSettings& s, bool on) { s.usage.showCodex = on; }, pageOn),
                 }},
                {L"Limits",
                 L"",
                 {
                     ChoiceRow(L"Warn at", L"A ring this full turns amber; red starts at 90%.", levels,
                               [](const AppSettings& s) { return s.usage.warnAt; }, [](AppSettings& s, double value) { s.usage.warnAt = value; },
                               pageOn),
                     ChoiceRow(L"Check Claude every", L"Only while the page is on screen.", refresh,
                               [](const AppSettings& s) { return s.usage.refreshMinutes; },
                               [](AppSettings& s, double value) { s.usage.refreshMinutes = value; }, pageOn),
                 }},
            },
            {}};
}

}  // namespace noitch
