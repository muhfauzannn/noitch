#pragma once

// Token usage read from the local logs of AI coding tools. Pure values in, values out: the source
// hands it bytes and the time, so all of it is unit-tested on any host. The macOS app uses the
// same rules (AIUsage.swift).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace noitch {

enum class AITool { Claude, Codex };

struct ModelPrice {
    double input;
    double output;
    double cacheRead;
};

// Dollars per million tokens. Cache writes cost 1.25x input for five minutes and 2x for an hour.
// Most specific first: "opus-5" would otherwise match "opus-5-5". Anthropic API list prices as of
// 2026-11; OpenAI only for models whose price is known. Anything else shows no cost.
inline std::optional<ModelPrice> FindPrice(std::string_view model) {
    struct Row {
        std::string_view name;
        ModelPrice price;
    };
    static constexpr Row kTable[] = {
        {"fable-5-2", {8, 40, 0.2}},      {"mythos-5-2", {8, 40, 0.2}},     {"fable-5", {10, 50, 1}},
        {"mythos-5", {10, 50, 1}},        {"opus-5-5", {4, 20, 0.2}},       {"opus-5", {5, 25, 0.5}},
        {"opus-4-8", {5, 25, 0.5}},       {"opus-4-7", {5, 25, 0.5}},       {"opus-4-6", {5, 25, 0.5}},
        {"opus-4-5", {5, 25, 0.5}},       {"opus-4", {15, 75, 1.5}},        {"sonnet-5-5", {2, 10, 0.1}},
        {"sonnet-5", {2, 10, 0.2}},       {"sonnet-4", {3, 15, 0.3}},       {"haiku-5-5", {0.1, 0.5, 0.01}},
        {"haiku-4-5", {1, 5, 0.1}},       {"gpt-5-mini", {0.25, 2, 0.025}}, {"gpt-5-nano", {0.05, 0.4, 0.005}},
        {"gpt-5-codex", {1.25, 10, 0.125}}, {"gpt-5.1-codex-mini", {0.25, 2, 0.025}}, {"gpt-5.1", {1.25, 10, 0.125}},
    };
    if (model == "gpt-5") return ModelPrice{1.25, 10, 0.125};
    for (const Row& row : kTable) {
        if (model.find(row.name) != std::string_view::npos) return row.price;
    }
    return std::nullopt;
}

struct UsageEntry {
    AITool tool = AITool::Claude;
    double time = 0;  // Seconds since 1970.
    std::string model;
    int64_t input = 0;  // Input not read from or written to a cache.
    int64_t output = 0;
    int64_t cacheWrite5m = 0;
    int64_t cacheWrite1h = 0;
    int64_t cacheRead = 0;

    int64_t Tokens() const { return input + output + cacheWrite5m + cacheWrite1h + cacheRead; }

    std::optional<double> Cost() const {
        const auto price = FindPrice(model);
        if (!price) return std::nullopt;
        return (static_cast<double>(input) * price->input + static_cast<double>(output) * price->output +
                static_cast<double>(cacheRead) * price->cacheRead + static_cast<double>(cacheWrite5m) * price->input * 1.25 +
                static_cast<double>(cacheWrite1h) * price->input * 2) / 1e6;
    }
};

// One usage window, such as five hours or a week, as the provider reports it.
struct LimitWindow {
    static constexpr double kFiveHours = 5 * 3600;
    static constexpr double kWeek = 7 * 86400;

    double usedPercent = 0;  // 0 to 100, and can pass 100 when a provider allows overage.
    std::optional<double> resetsAt;  // Seconds since 1970.
    double length = 0;

    bool operator==(const LimitWindow&) const = default;

    // How much of the window's time has gone, from 0 to 1.
    std::optional<double> Elapsed(double now) const {
        if (!resetsAt) return std::nullopt;
        return std::clamp(1 - (*resetsAt - now) / length, 0.0, 1.0);
    }
    std::optional<double> Remaining(double now) const {
        if (!resetsAt) return std::nullopt;
        return std::max(*resetsAt - now, 0.0);
    }
    // A window whose reset has passed starts again from nothing.
    LimitWindow Current(double now) const {
        if (!resetsAt || *resetsAt > now) return *this;
        return {0, std::nullopt, length};
    }
    // Percent per second, averaged over the window so far. Too early in a window says nothing.
    std::optional<double> AverageRate(double now) const {
        const auto elapsed = Elapsed(now);
        if (!elapsed || *elapsed * length < 600) return std::nullopt;
        return usedPercent / (*elapsed * length);
    }
};

// A snapshot of one provider's limits at a moment, from Codex's logs.
struct LimitSample {
    double time = 0;
    std::optional<LimitWindow> fiveHour;
    std::optional<LimitWindow> week;
};

// What Codex remembers between lines of one file.
struct CodexFileState {
    std::string model;
    // Codex repeats a usage event when only the limits change; a total that didn't grow is a repeat.
    int64_t lastTotal = 0;
};

// Finds one field of a JSON object without building the whole object. Log lines can hold large
// tool output, and only a few numbers are needed from each.
namespace json_scan {

inline size_t SkipSpace(std::string_view s, size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    return i;
}

// The index of the closing quote of the string that opens at `i`.
inline size_t StringEnd(std::string_view s, size_t i) {
    for (++i; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
        } else if (s[i] == '"') {
            return i;
        }
    }
    return std::string_view::npos;
}

// One past the end of the value that starts at `i`.
inline size_t ValueEnd(std::string_view s, size_t i) {
    if (i >= s.size()) return std::string_view::npos;
    if (s[i] == '"') {
        const size_t end = StringEnd(s, i);
        return end == std::string_view::npos ? end : end + 1;
    }
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        for (; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '"') {
                i = StringEnd(s, i);
                if (i == std::string_view::npos) return i;
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::string_view::npos;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') ++i;
    return i;
}

// The raw value of `key` among the object's own fields, not those of nested objects.
inline std::optional<std::string_view> Value(std::string_view key, std::string_view object) {
    size_t i = SkipSpace(object, 0);
    if (i >= object.size() || object[i] != '{') return std::nullopt;
    ++i;
    for (;;) {
        i = SkipSpace(object, i);
        if (i >= object.size() || object[i] != '"') return std::nullopt;
        const size_t keyEnd = StringEnd(object, i);
        if (keyEnd == std::string_view::npos) return std::nullopt;
        const std::string_view name = object.substr(i + 1, keyEnd - i - 1);
        i = SkipSpace(object, keyEnd + 1);
        if (i >= object.size() || object[i] != ':') return std::nullopt;
        const size_t start = SkipSpace(object, i + 1);
        const size_t end = ValueEnd(object, start);
        if (end == std::string_view::npos) return std::nullopt;
        if (name == key) return object.substr(start, end - start);
        i = SkipSpace(object, end);
        if (i >= object.size() || object[i] != ',') return std::nullopt;
        ++i;
    }
}

// A string value without escapes resolved: model names, ids and timestamps have none.
inline std::optional<std::string_view> String(std::optional<std::string_view> value) {
    if (!value || value->size() < 2 || value->front() != '"' || value->back() != '"') return std::nullopt;
    return value->substr(1, value->size() - 2);
}

inline std::optional<double> Number(std::optional<std::string_view> value) {
    if (!value || value->empty()) return std::nullopt;
    const char first = value->front();
    if (first != '-' && (first < '0' || first > '9')) return std::nullopt;
    char buffer[64];
    const size_t length = std::min(value->size(), sizeof(buffer) - 1);
    std::memcpy(buffer, value->data(), length);
    buffer[length] = '\0';
    return std::strtod(buffer, nullptr);
}

inline int64_t Int(std::string_view key, std::string_view object) {
    const auto number = Number(Value(key, object));
    return number ? static_cast<int64_t>(*number) : 0;
}

}  // namespace json_scan

// ISO 8601 in UTC or with an offset, as both tools write it.
inline std::optional<double> ParseTime(std::string_view text) {
    const auto digits = [&](size_t start, size_t count) -> int {
        if (start + count > text.size()) return -1;
        int value = 0;
        for (size_t i = start; i < start + count; ++i) {
            if (text[i] < '0' || text[i] > '9') return -1;
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };
    const int year = digits(0, 4), month = digits(5, 2), day = digits(8, 2);
    const int hour = digits(11, 2), minute = digits(14, 2), second = digits(17, 2);
    if (year < 0 || month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0) return std::nullopt;
    size_t i = 19;
    double fraction = 0;
    if (i < text.size() && text[i] == '.') {
        double scale = 0.1;
        for (++i; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
            fraction += (text[i] - '0') * scale;
            scale /= 10;
        }
    }
    int offset = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        const int h = digits(i + 1, 2), m = digits(i + 4, 2);
        if (h < 0 || m < 0) return std::nullopt;
        offset = (h * 3600 + m * 60) * (text[i] == '+' ? 1 : -1);
    }
    // Days from the civil calendar, valid for any Gregorian date.
    const int64_t y = month <= 2 ? year - 1 : year;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yearOfEra = y - era * 400;
    const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const int64_t days = era * 146097 + dayOfEra - 719468;
    return static_cast<double>(days * 86400 + hour * 3600 + minute * 60 + second - offset) + fraction;
}

// Entries from the last day, kept small: a few numbers per request, never the log text.
class UsageLog {
public:

    const std::vector<UsageEntry>& Entries() const { return entries_; }
    // Codex writes its limits with every turn, so their history gives a recent pace.
    const std::vector<LimitSample>& CodexSamples() const { return codexSamples_; }

    // Reads whole lines and returns how many bytes it used, so a half-written last line is read later.
    size_t ReadClaude(std::string_view bytes, double cutoff) {
        return ForEachLine(bytes, {"\"usage\"", {}}, [&](std::string_view line, int) {
            UsageEntry entry;
            std::string key;
            if (!ParseClaude(line, &entry, &key) || entry.time < cutoff || entry.Tokens() <= 0) return;
            if (!key.empty()) {
                if (seenClaude_.count(key)) return;
                seenClaude_.emplace(std::move(key), entry.time);
            }
            entries_.push_back(std::move(entry));
        });
    }

    size_t ReadCodex(std::string_view bytes, CodexFileState& state, double cutoff) {
        return ForEachLine(bytes, {"\"turn_context\"", "\"token_count\""}, [&](std::string_view line, int pattern) {
            using namespace json_scan;
            const auto payload = Value("payload", line);
            if (!payload) return;
            if (pattern == 0) {
                if (const auto model = String(Value("model", *payload))) state.model = std::string(*model);
                return;
            }
            const auto stamp = String(Value("timestamp", line));
            const auto time = stamp ? ParseTime(*stamp) : std::nullopt;
            if (!time) return;
            if (const auto limits = Value("rate_limits", *payload)) {
                if (codexSamples_.empty() || *time >= codexSamples_.back().time) {
                    LimitSample sample{*time, CodexWindow(Value("primary", *limits)), CodexWindow(Value("secondary", *limits))};
                    if (sample.fiveHour || sample.week) codexSamples_.push_back(sample);
                }
            }
            const auto info = Value("info", *payload);
            if (!info) return;
            const auto total = Value("total_token_usage", *info);
            const auto last = Value("last_token_usage", *info);
            if (!total || !last) return;
            const int64_t totalTokens = Int("total_tokens", *total);
            if (totalTokens <= state.lastTotal) return;
            state.lastTotal = totalTokens;
            if (*time < cutoff) return;
            UsageEntry entry;
            entry.tool = AITool::Codex;
            entry.time = *time;
            entry.model = state.model.empty() ? "codex" : state.model;
            const int64_t input = Int("input_tokens", *last);
            const int64_t cached = Int("cached_input_tokens", *last);
            // OpenAI counts cached input inside input; split it out to price it.
            entry.input = std::max<int64_t>(input - cached, 0);
            entry.cacheRead = cached;
            entry.cacheWrite5m = Int("cache_write_input_tokens", *last);
            entry.output = Int("output_tokens", *last);
            if (entry.Tokens() > 0) entries_.push_back(std::move(entry));
        });
    }

    void Prune(double cutoff, double sampleCutoff) {
        // The latest sample stays however old it is: it still says where the week stands.
        if (!codexSamples_.empty()) {
            const LimitSample last = codexSamples_.back();
            codexSamples_.erase(std::remove_if(codexSamples_.begin(), codexSamples_.end(), [&](const LimitSample& s) { return s.time < sampleCutoff; }), codexSamples_.end());
            if (codexSamples_.empty()) codexSamples_.push_back(last);
        }
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const UsageEntry& e) { return e.time < cutoff; }), entries_.end());
        for (auto it = seenClaude_.begin(); it != seenClaude_.end();) {
            it = it->second < cutoff ? seenClaude_.erase(it) : std::next(it);
        }
    }

private:
    static std::optional<LimitWindow> CodexWindow(std::optional<std::string_view> object) {
        using namespace json_scan;
        if (!object) return std::nullopt;
        const auto used = Number(Value("used_percent", *object));
        const auto minutes = Number(Value("window_minutes", *object));
        if (!used || !minutes || *minutes <= 0) return std::nullopt;
        return LimitWindow{*used, Number(Value("resets_at", *object)), *minutes * 60};
    }

    // Calls `body` for each complete line holding one of the patterns, in file order, with the index
    // of the first pattern found. memchr and find skip the rest quickly: most of a log is tool output.
    template <typename Body>
    static size_t ForEachLine(std::string_view bytes, const std::string_view (&patterns)[2], Body body) {
        size_t start = 0;
        while (start < bytes.size()) {
            const void* found = std::memchr(bytes.data() + start, '\n', bytes.size() - start);
            if (!found) break;
            const size_t end = static_cast<size_t>(static_cast<const char*>(found) - bytes.data());
            const std::string_view line = bytes.substr(start, end - start);
            for (int index = 0; index < 2; ++index) {
                if (!patterns[index].empty() && line.find(patterns[index]) != std::string_view::npos) {
                    body(line, index);
                    break;
                }
            }
            start = end + 1;
        }
        return start;
    }

    // Claude Code lines carry the whole response, often hundreds of kilobytes, and only a few fields
    // are needed. Quotes inside JSON strings are always escaped, so `"key":` can only match a real
    // key, and a search finds each one without walking the text.
    static std::optional<std::string_view> StringAfter(std::string_view pattern, std::string_view line) {
        const size_t at = line.find(pattern);
        if (at == std::string_view::npos) return std::nullopt;
        size_t i = at + pattern.size();
        const size_t start = i;
        while (i < line.size() && line[i] != '"') i += line[i] == '\\' ? 2 : 1;
        if (i >= line.size()) return std::nullopt;
        return line.substr(start, i - start);
    }

    static bool ParseClaude(std::string_view line, UsageEntry* entry, std::string* key) {
        using namespace json_scan;
        const size_t at = line.find("\"usage\":");
        if (at == std::string_view::npos) return false;
        const size_t start = at + 8;
        const size_t end = ValueEnd(line, start);
        if (end == std::string_view::npos) return false;
        const std::string_view usage = line.substr(start, end - start);
        const auto stamp = StringAfter("\"timestamp\":\"", line);
        const auto time = stamp ? ParseTime(*stamp) : std::nullopt;
        if (!time) return false;
        const auto model = StringAfter("\"model\":\"", line);
        if (model && *model == "<synthetic>") return false;
        entry->tool = AITool::Claude;
        entry->time = *time;
        entry->model = model ? std::string(*model) : "claude";
        entry->input = Int("input_tokens", usage);
        entry->output = Int("output_tokens", usage);
        entry->cacheRead = Int("cache_read_input_tokens", usage);
        const int64_t written = Int("cache_creation_input_tokens", usage);
        // The split by lifetime is newer than the total; without it, price all of it as five minutes.
        const auto creation = Value("cache_creation", usage);
        const int64_t longLived = creation ? Int("ephemeral_1h_input_tokens", *creation) : 0;
        entry->cacheWrite1h = std::min(longLived, written);
        entry->cacheWrite5m = written - entry->cacheWrite1h;
        if (const auto id = StringAfter("\"id\":\"msg_", line)) {
            const auto request = StringAfter("\"requestId\":\"", line);
            *key = std::string(*id) + ":" + std::string(request.value_or(std::string_view{}));
        }
        return true;
    }

    std::vector<UsageEntry> entries_;
    std::vector<LimitSample> codexSamples_;
    // Claude Code writes one line per content block, each repeating the request's usage.
    std::unordered_map<std::string, double> seenClaude_;
};

// What the page shows for one tool.
struct ToolUsage {
    struct Model {
        std::string name;
        int64_t tokens = 0;
        std::optional<double> cost;
    };

    int64_t tokens = 0;
    double cost = 0;
    int64_t unpricedTokens = 0;  // Tokens from models without a price: the cost is a lower bound.
    std::vector<Model> models;

    template <typename Filter>
    static ToolUsage From(const std::vector<UsageEntry>& entries, Filter include) {
        ToolUsage usage;
        for (const UsageEntry& entry : entries) {
            if (!include(entry)) continue;
            const int64_t entryTokens = entry.Tokens();
            usage.tokens += entryTokens;
            const auto entryCost = entry.Cost();
            if (entryCost) {
                usage.cost += *entryCost;
            } else {
                usage.unpricedTokens += entryTokens;
            }
            auto it = std::find_if(usage.models.begin(), usage.models.end(), [&](const Model& m) { return m.name == entry.model; });
            if (it == usage.models.end()) {
                usage.models.push_back({entry.model, 0, entryCost ? std::optional<double>(0) : std::nullopt});
                it = usage.models.end() - 1;
            }
            it->tokens += entryTokens;
            if (entryCost) it->cost = it->cost.value_or(0) + *entryCost;
        }
        std::sort(usage.models.begin(), usage.models.end(), [](const Model& a, const Model& b) {
            return a.tokens != b.tokens ? a.tokens > b.tokens : a.name < b.name;
        });
        return usage;
    }
};

// Today's totals per tool, for the line under each tool's name.
struct UsageSummary {
    ToolUsage claudeToday;
    ToolUsage codexToday;
};

inline UsageSummary Summarize(const UsageLog& log, double now, double dayStart) {
    UsageSummary summary;
    const auto& entries = log.Entries();
    summary.claudeToday = ToolUsage::From(entries, [&](const UsageEntry& e) { return e.tool == AITool::Claude && e.time >= dayStart && e.time <= now; });
    summary.codexToday = ToolUsage::From(entries, [&](const UsageEntry& e) { return e.tool == AITool::Codex && e.time >= dayStart && e.time <= now; });
    return summary;
}

}  // namespace noitch
