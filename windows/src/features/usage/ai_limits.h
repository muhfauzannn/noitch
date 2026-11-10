#pragma once

// The two limits that matter, how fast they are being used, and what that means. Pure values in,
// values out: the sources hand it numbers and the time. No Windows headers. The macOS app uses the
// same rules (AILimits.swift).

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ai_usage.h"

namespace noitch {

// Whether usage is running ahead of the clock. Using 60% of a window that is 30% through is ahead:
// the same pace would hit the limit before the reset.
enum class Pace { Under, Even, Ahead };

inline std::optional<Pace> PaceOf(const LimitWindow& window, double now) {
    const auto elapsed = window.Elapsed(now);
    if (!elapsed || *elapsed < 0.05) return std::nullopt;
    const double used = window.usedPercent / 100;
    return used > *elapsed + 0.1 ? Pace::Ahead : used < *elapsed - 0.1 ? Pace::Under : Pace::Even;
}

// Why a tool has no numbers, when it hasn't.
enum class LimitProblem { None, Loading, SignedOut, Expired, Unavailable, NoData };

struct ToolLimits {
    std::optional<LimitWindow> fiveHour;
    std::optional<LimitWindow> week;
    std::optional<double> recentRate;  // Five-hour percent per second over the last half hour.
    LimitProblem problem = LimitProblem::None;

    static ToolLimits Failed(LimitProblem problem) {
        ToolLimits limits;
        limits.problem = problem;
        return limits;
    }
};

enum class Severity { Calm, Watch, Critical };

// The one thing worth saying about a tool's limits right now, most urgent first.
struct LimitInsight {
    enum class Kind { Reached, RunsOut, OnTrack, Idle, Problem } kind = Kind::Idle;
    double window = 0;                 // Reached, RunsOut: the window's length.
    double time = 0;                   // RunsOut: seconds until it runs out.
    std::optional<double> resetsIn;    // Reached, RunsOut.
    double value = 0;                  // OnTrack: projected percent. Idle: used percent.
    LimitProblem problem = LimitProblem::None;

    Severity Level() const {
        switch (kind) {
            case Kind::Reached: return Severity::Critical;
            case Kind::RunsOut: return window == LimitWindow::kFiveHours && time < 1800 ? Severity::Critical : Severity::Watch;
            case Kind::OnTrack: return value >= 85 ? Severity::Watch : Severity::Calm;
            default: return Severity::Calm;
        }
    }
};

inline LimitInsight InsightOf(const ToolLimits& limits, double now) {
    LimitInsight insight;
    if (limits.problem != LimitProblem::None) {
        insight.kind = LimitInsight::Kind::Problem;
        insight.problem = limits.problem;
        return insight;
    }
    const std::optional<LimitWindow> five = limits.fiveHour ? std::optional(limits.fiveHour->Current(now)) : std::nullopt;
    const std::optional<LimitWindow> week = limits.week ? std::optional(limits.week->Current(now)) : std::nullopt;
    for (const auto* window : {&five, &week}) {
        if (*window && (*window)->usedPercent >= 100) {
            insight.kind = LimitInsight::Kind::Reached;
            insight.window = (*window)->length;
            insight.resetsIn = (*window)->Remaining(now);
            return insight;
        }
    }
    // The five-hour window moves fast, so its recent pace says more than its average.
    if (five) {
        const auto remaining = five->Remaining(now);
        const auto rate = limits.recentRate ? limits.recentRate : five->AverageRate(now);
        if (remaining && rate && *rate > 0) {
            const double time = (100 - five->usedPercent) / *rate;
            if (time < *remaining) {
                insight.kind = LimitInsight::Kind::RunsOut;
                insight.window = five->length;
                insight.time = time;
                insight.resetsIn = remaining;
                return insight;
            }
        }
    }
    if (week) {
        const auto remaining = week->Remaining(now);
        const auto rate = week->AverageRate(now);
        if (remaining && rate && *rate > 0) {
            const double time = (100 - week->usedPercent) / *rate;
            if (time < *remaining) {
                insight.kind = LimitInsight::Kind::RunsOut;
                insight.window = week->length;
                insight.time = time;
                insight.resetsIn = remaining;
                return insight;
            }
        }
    }
    if (five) {
        const auto remaining = five->Remaining(now);
        const auto rate = limits.recentRate ? limits.recentRate : five->AverageRate(now);
        if (remaining && rate) {
            insight.kind = LimitInsight::Kind::OnTrack;
            insight.value = std::min(five->usedPercent + *rate * *remaining, 100.0);
            return insight;
        }
    }
    insight.kind = LimitInsight::Kind::Idle;
    insight.value = five ? five->usedPercent : week ? week->usedPercent : 0;
    return insight;
}

inline constexpr double kRecentSpan = 1800;

// Codex's five-hour pace over the last half hour, from its own reported percentages. No samples in
// that time means no use, which is a pace of zero.
inline std::optional<double> CodexRate(const std::vector<LimitSample>& samples, double now) {
    if (samples.empty() || !samples.back().fiveHour || !samples.back().fiveHour->resetsAt) return std::nullopt;
    const LimitWindow& window = *samples.back().fiveHour;
    const double resets = *window.resetsAt;
    if (resets <= now) return std::nullopt;
    const double start = now - kRecentSpan;
    const double windowStart = resets - window.length;
    std::optional<double> base;
    for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
        if (it->time <= start && it->fiveHour && it->fiveHour->resetsAt == resets) {
            base = it->fiveHour->usedPercent;
            break;
        }
    }
    if (!base && windowStart >= start) base = 0.0;
    if (!base) return std::nullopt;
    const double span = now - std::max(start, windowStart);
    if (span < 300) return std::nullopt;
    return std::max(window.usedPercent - *base, 0.0) / span;
}

// Claude's five-hour pace, from the share of the window's cost spent in the last half hour of local
// logs. The provider reports only the percentage, and cost tracks what the limit counts better than
// raw tokens do.
inline std::optional<double> ClaudeRate(const std::vector<UsageEntry>& entries, const LimitWindow& window, double now) {
    if (!window.resetsAt || *window.resetsAt <= now) return std::nullopt;
    const double windowStart = *window.resetsAt - window.length;
    double total = 0;
    double recent = 0;
    for (const UsageEntry& entry : entries) {
        if (entry.tool != AITool::Claude || entry.time < windowStart || entry.time > now) continue;
        const double weight = entry.Cost().value_or(static_cast<double>(entry.Tokens()) / 1e6);
        total += weight;
        if (entry.time >= now - kRecentSpan) recent += weight;
    }
    const double span = std::min(kRecentSpan, now - windowStart);
    if (total <= 0 || span < 300) return std::nullopt;
    return window.usedPercent * (recent / total) / span;
}

// Reads Anthropic's usage response and Claude Code's stored login. Kept pure, so the shapes are
// tested without a network or a credentials file.
inline std::optional<ToolLimits> ClaudeLimitsFromResponse(std::string_view body) {
    using namespace json_scan;
    const auto window = [&](std::string_view key, double length) -> std::optional<LimitWindow> {
        const auto object = Value(key, body);
        if (!object) return std::nullopt;
        const auto used = Number(Value("utilization", *object));
        if (!used) return std::nullopt;
        std::optional<double> resets;
        const auto raw = Value("resets_at", *object);
        if (const auto text = String(raw)) {
            resets = ParseTime(*text);
        } else {
            resets = Number(raw);
        }
        return LimitWindow{*used, resets, length};
    };
    ToolLimits limits;
    limits.fiveHour = window("five_hour", LimitWindow::kFiveHours);
    limits.week = window("seven_day", LimitWindow::kWeek);
    if (!limits.fiveHour && !limits.week) return std::nullopt;
    return limits;
}

struct ClaudeLogin {
    std::string accessToken;
    std::optional<double> expiresAt;  // Seconds since 1970.
};

inline std::optional<ClaudeLogin> ClaudeLoginFromCredentials(std::string_view body) {
    using namespace json_scan;
    const auto oauth = Value("claudeAiOauth", body);
    if (!oauth) return std::nullopt;
    const auto token = String(Value("accessToken", *oauth));
    if (!token || token->empty()) return std::nullopt;
    ClaudeLogin login{std::string(*token), std::nullopt};
    if (const auto expires = Number(Value("expiresAt", *oauth))) login.expiresAt = *expires / 1000;
    return login;
}

}  // namespace noitch
