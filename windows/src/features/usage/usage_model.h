#pragma once

// What the usage page shows, as text. Pure, so the wording is tested; the macOS app matches it
// (UsageModel.swift).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include "ai_usage.h"

namespace noitch {

inline constexpr int kUsageRows = 3;

struct UsageRow {
    std::string name;
    std::string value;

    bool operator==(const UsageRow&) const = default;
};

struct UsageColumn {
    std::string title;
    std::string tokens;
    std::string cost;
    std::string limit;
    std::optional<double> limitFraction;  // How full the five-hour window is, from 0 to 1.
    UsageRow rows[kUsageRows];
    int rowCount = 0;

    bool operator==(const UsageColumn&) const = default;
};

namespace usage_text {

inline std::string Trimmed(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), value >= 100 ? "%.0f" : "%.1f", value);
    std::string text = buffer;
    if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) text.resize(text.size() - 2);
    return text;
}

inline std::string Tokens(int64_t count) {
    const double value = static_cast<double>(count);
    if (count < 1000) return std::to_string(count);
    if (count < 1000000) return Trimmed(value / 1e3) + "K";
    if (count < 1000000000) return Trimmed(value / 1e6) + "M";
    return Trimmed(value / 1e9) + "B";
}

inline std::string Dollars(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), value >= 100 ? "$%.0f" : "$%.2f", value);
    return buffer;
}

// A lower bound when some models have no price, and a dash when none do.
inline std::string Cost(const ToolUsage& usage) {
    if (usage.tokens > 0 && usage.unpricedTokens == usage.tokens) return "\xE2\x80\x94";
    return Dollars(usage.cost) + (usage.unpricedTokens > 0 ? "+" : "");
}

inline std::string Duration(double seconds) {
    const int minutes = std::max(static_cast<int>(std::ceil(seconds / 60)), 0);
    return minutes >= 60 ? std::to_string(minutes / 60) + "h " + std::to_string(minutes % 60) + "m" : std::to_string(minutes) + "m";
}

// "claude-opus-5-5-20260101" reads as "Opus 5.5". Other tools' names are already short.
inline std::string DisplayName(const std::string& model) {
    const std::string prefix = "claude-";
    if (model.compare(0, prefix.size(), prefix) != 0) return model;
    std::string rest = model.substr(prefix.size());
    const size_t lastDash = rest.rfind('-');
    if (lastDash != std::string::npos && rest.size() - lastDash - 1 == 8 &&
        std::all_of(rest.begin() + static_cast<long>(lastDash) + 1, rest.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        rest.resize(lastDash);
    }
    const size_t dash = rest.find('-');
    std::string family = rest.substr(0, dash);
    if (family.empty()) return model;
    if (family[0] >= 'a' && family[0] <= 'z') family[0] = static_cast<char>(family[0] - 'a' + 'A');
    if (dash == std::string::npos) return family;
    std::string version = rest.substr(dash + 1);
    std::replace(version.begin(), version.end(), '-', '.');
    return family + " " + version;
}

}  // namespace usage_text

struct UsageModel {
    UsageColumn claude;
    UsageColumn codex;

    bool operator==(const UsageModel&) const = default;
};

inline UsageColumn MakeUsageColumn(const char* title, const ToolUsage& usage) {
    using namespace usage_text;
    UsageColumn column;
    column.title = title;
    column.tokens = Tokens(usage.tokens);
    column.cost = Cost(usage);
    for (const auto& model : usage.models) {
        if (column.rowCount == kUsageRows) break;
        column.rows[column.rowCount++] = {DisplayName(model.name), Tokens(model.tokens) + (model.cost ? " \xC2\xB7 " + Dollars(*model.cost) : "")};
    }
    if (column.rowCount == 0) column.rows[column.rowCount++] = {"No usage yet", ""};
    return column;
}

inline UsageModel MakeUsageModel(const UsageSummary& summary, double now) {
    using namespace usage_text;
    const std::string dot = " \xC2\xB7 ";
    UsageModel model;
    model.claude = MakeUsageColumn("Claude Code \xC2\xB7 today", summary.claudeToday);
    if (summary.claudeBlock) {
        const UsageBlock& block = *summary.claudeBlock;
        model.claude.limit = "5h block" + dot + Tokens(block.usage.tokens) + dot + Cost(block.usage) + dot + "resets in " + Duration(block.End() - now);
        model.claude.limitFraction = std::clamp((now - block.start) / UsageLog::kBlockLength, 0.0, 1.0);
    } else {
        model.claude.limit = "No active 5h block";
    }
    model.codex = MakeUsageColumn("Codex \xC2\xB7 today", summary.codexToday);
    if (summary.codexLimit) {
        model.codex.limit = "5h limit " + std::to_string(static_cast<int>(std::lround(summary.codexLimit->usedPercent))) + "%" + dot + "resets in " +
                            Duration(summary.codexLimit->resetsAt - now);
        model.codex.limitFraction = std::clamp(summary.codexLimit->usedPercent / 100, 0.0, 1.0);
    } else {
        model.codex.limit = "5h limit not reported yet";
    }
    return model;
}

}  // namespace noitch
