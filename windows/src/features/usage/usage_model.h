#pragma once

// What the usage page shows, as values and text. Pure, so the wording is tested; the macOS app
// matches it (UsageModel.swift).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>

#include "ai_limits.h"
#include "ai_usage.h"

namespace noitch {

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
    if (minutes >= 24 * 60) return std::to_string(minutes / 1440) + "d " + std::to_string(minutes % 1440 / 60) + "h";
    return minutes >= 60 ? std::to_string(minutes / 60) + "h " + std::to_string(minutes % 60) + "m" : std::to_string(minutes) + "m";
}

inline std::string Percent(double value) { return std::to_string(static_cast<int>(std::lround(value))) + "%"; }

}  // namespace usage_text

// Turns a time into a short local label such as "Thu 14:00".
using UsageClock = std::function<std::string(double)>;

struct UsageModel {
    // One ring of the gauge: how full the window is, and where the clock is in it.
    struct Ring {
        double fraction = 0;
        std::optional<double> tick;  // How much of the window's time has passed. Fill past it is ahead of pace.
        Severity severity = Severity::Calm;
        bool operator==(const Ring&) const = default;
    };

    struct Row {
        std::string label;
        std::string value;
        std::optional<Pace> pace;
        std::string reset;
        Severity severity = Severity::Calm;
        bool operator==(const Row&) const = default;
    };

    struct Tool {
        std::string name;
        std::string today;
        std::string center;
        std::string caption;
        std::optional<Ring> inner;  // Five hours.
        std::optional<Ring> outer;  // The week.
        Row rows[2];
        std::string headline;
        std::string detail;
        Severity severity = Severity::Calm;
        bool operator==(const Tool&) const = default;
    };

    Tool claude;
    Tool codex;
    bool operator==(const UsageModel&) const = default;
};

inline std::pair<std::string, std::string> InsightText(const LimitInsight& insight, double now, const UsageClock& clock) {
    using namespace usage_text;
    const auto name = [](double length) { return std::string(length == LimitWindow::kFiveHours ? "5h" : "Weekly"); };
    switch (insight.kind) {
        case LimitInsight::Kind::Reached: {
            if (!insight.resetsIn) return {name(insight.window) + " limit reached", "Waiting for the reset"};
            const std::string back = insight.window == LimitWindow::kFiveHours ? "in " + Duration(*insight.resetsIn) : clock(now + *insight.resetsIn);
            return {name(insight.window) + " limit reached", "Back " + back};
        }
        case LimitInsight::Kind::RunsOut: {
            // How early it runs out is the part worth acting on: that long with nothing left.
            const std::string early = Duration(insight.resetsIn.value_or(0) - insight.time) + " before its reset";
            if (insight.window == LimitWindow::kFiveHours) return {"5h runs out in ~" + Duration(insight.time), early};
            return {"Week runs out ~" + clock(now + insight.time), early};
        }
        case LimitInsight::Kind::OnTrack: {
            const char* headline = insight.value < 50 ? "Plenty of room" : insight.value < 85 ? "On track" : "Cutting it close";
            return {headline, "~" + Percent(insight.value) + " of 5h by its reset"};
        }
        case LimitInsight::Kind::Idle:
            return {"Quiet", insight.value > 0 ? Percent(insight.value) + " used so far" : "Nothing used in this window"};
        case LimitInsight::Kind::Problem:
            switch (insight.problem) {
                case LimitProblem::Loading: return {"Checking limits\xE2\x80\xA6", "Asking Anthropic"};
                case LimitProblem::SignedOut: return {"Not signed in", "Sign in to Claude Code"};
                case LimitProblem::Expired: return {"Login expired", "Run claude in a terminal"};
                case LimitProblem::NoData: return {"No limits yet", "Use it once to see them"};
                default: return {"Limits unavailable", "Trying again in a minute"};
            }
    }
    return {"", ""};
}

inline UsageModel::Tool MakeUsageTool(const char* name, const ToolLimits& limits, const ToolUsage& today, double now, const UsageClock& clock) {
    using namespace usage_text;
    const LimitInsight insight = InsightOf(limits, now);
    const std::optional<LimitWindow> five = limits.fiveHour ? std::optional(limits.fiveHour->Current(now)) : std::nullopt;
    const std::optional<LimitWindow> week = limits.week ? std::optional(limits.week->Current(now)) : std::nullopt;
    const auto severity = [&](const LimitWindow& window) {
        Severity level = window.usedPercent >= 90 ? Severity::Critical : window.usedPercent >= 70 ? Severity::Watch : Severity::Calm;
        if (PaceOf(window, now) == Pace::Ahead) level = std::max(level, Severity::Watch);
        if ((insight.kind == LimitInsight::Kind::Reached || insight.kind == LimitInsight::Kind::RunsOut) && insight.window == window.length) {
            level = std::max(level, insight.Level());
        }
        return level;
    };
    const auto ring = [&](const std::optional<LimitWindow>& window) -> std::optional<UsageModel::Ring> {
        if (!window) return std::nullopt;
        return UsageModel::Ring{std::clamp(window->usedPercent / 100, 0.0, 1.0), window->Elapsed(now), severity(*window)};
    };
    const auto row = [&](const char* label, const std::optional<LimitWindow>& window, bool isWeek) {
        if (!window) return UsageModel::Row{label, "\xE2\x80\x94", std::nullopt, "", Severity::Calm};
        std::string reset;
        if (const auto remaining = window->Remaining(now)) {
            reset = isWeek ? clock(now + *remaining) : Duration(*remaining);
        } else if (limits.problem == LimitProblem::None) {
            reset = "reset";
        }
        return UsageModel::Row{label, Percent(window->usedPercent), PaceOf(*window, now), reset, severity(*window)};
    };
    const auto [headline, detail] = InsightText(insight, now, clock);
    UsageModel::Tool tool;
    tool.name = name;
    // Remembered numbers say so, in place of today's totals.
    if (limits.asOf) {
        tool.today = "As of " + clock(*limits.asOf);
    } else {
        tool.today = today.tokens > 0 ? "Today " + Tokens(today.tokens) + " \xC2\xB7 " + Cost(today) : "Nothing today";
    }
    const auto& center = five ? five : week;
    tool.center = center ? Percent(center->usedPercent) : "\xE2\x80\x94";
    tool.caption = five ? "5h" : week ? "week" : "";
    tool.inner = ring(five);
    tool.outer = ring(week);
    tool.rows[0] = row("5h", five, false);
    tool.rows[1] = row("Week", week, true);
    tool.headline = headline;
    tool.detail = detail;
    tool.severity = insight.Level();
    return tool;
}

inline UsageModel MakeUsageModel(const ToolLimits& claude, const ToolLimits& codex, const UsageSummary& summary, double now, const UsageClock& clock) {
    return {MakeUsageTool("Claude Code", claude, summary.claudeToday, now, clock), MakeUsageTool("Codex", codex, summary.codexToday, now, clock)};
}

}  // namespace noitch
