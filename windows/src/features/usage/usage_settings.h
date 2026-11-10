#pragma once

// The usage page's preferences. No Windows headers, so tests run on any host. Matches
// UsageSettings.swift.

#include <algorithm>
#include <vector>

namespace noitch {

enum class UsageTool { Claude, Codex };

struct UsageSettings {
    bool showClaude = true;
    bool showCodex = true;
    // A window this full or more turns amber. Red stays at 90%, or above this when it is higher.
    double warnAt = 70;
    // Minutes between requests for Claude's limits while the page shows. Never under one, the
    // approved rate; the local logs are still read every minute.
    double refreshMinutes = 1;

    double CriticalAt() const { return std::max(90.0, warnAt + 5); }

    // The tools shown, in column order. Never empty: with both turned off, Claude stays.
    std::vector<UsageTool> Tools() const {
        std::vector<UsageTool> tools;
        if (showClaude) tools.push_back(UsageTool::Claude);
        if (showCodex) tools.push_back(UsageTool::Codex);
        if (tools.empty()) tools.push_back(UsageTool::Claude);
        return tools;
    }

    bool operator==(const UsageSettings&) const = default;
};

inline constexpr double kWarnLevels[] = {50, 60, 70, 80};
inline constexpr double kRefreshChoices[] = {1, 5, 15};

}  // namespace noitch
