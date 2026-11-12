#pragma once

// Moodle tasks: reading the web service's answers, and the rules behind the list, the alerts and the
// reminders. No Windows headers, so tests run on any host. Matches MoodleFormat.swift and
// TaskModel.swift.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "features/lyrics/lyrics.h"
#include "features/usage/ai_limits.h"

namespace noitch {

enum class TaskModule { Assignment, Quiz, Forum, Other };

inline TaskModule TaskModuleFromName(std::string_view name) {
    if (name == "assign") return TaskModule::Assignment;
    if (name == "quiz") return TaskModule::Quiz;
    if (name == "forum" || name == "hsuforum") return TaskModule::Forum;
    return TaskModule::Other;
}

inline const char* TaskModuleKey(TaskModule module) {
    switch (module) {
        case TaskModule::Assignment: return "assignment";
        case TaskModule::Quiz: return "quiz";
        case TaskModule::Forum: return "forum";
        default: return "other";
    }
}

inline TaskModule TaskModuleFromKey(std::string_view key) {
    if (key == "assignment") return TaskModule::Assignment;
    if (key == "quiz") return TaskModule::Quiz;
    if (key == "forum") return TaskModule::Forum;
    return TaskModule::Other;
}

// One thing still to do on Moodle. Moodle drops it from the list once it is done.
struct MoodleTask {
    int64_t id = 0;
    std::string title;
    // A short label for the course, such as "PBP".
    std::string course;
    TaskModule module = TaskModule::Other;
    // When it is due, in seconds since 1970.
    double due = 0;
    std::string url;

    bool operator==(const MoodleTask&) const = default;
};

enum class MoodleProblem { None, SignedOut, WrongLogin, NoToken, Maintenance, Unavailable };

struct MoodleSiteInfo {
    std::string userName;
    std::string siteName;
    std::set<std::string> functions;
};

namespace task_text {

// Moodle before 4.0 has no activity name on an event, only "Tugas 3 is due".
inline std::string ActivityTitle(std::string name) {
    for (const std::string_view suffix : {" is due to be graded", " is due", " closes", " should be completed", " opens"}) {
        if (name.size() > suffix.size() && std::string_view(name).substr(name.size() - suffix.size()) == suffix) {
            name.resize(name.size() - suffix.size());
            return name;
        }
    }
    return name;
}

inline void ReplaceAll(std::string& text, std::string_view from, std::string_view to) {
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
}

// Names arrive with HTML entities, and sometimes with tags from multi-language filters.
inline std::string PlainText(std::string_view text) {
    std::string out;
    bool inTag = false;
    for (const char c : text) {
        if (c == '<') {
            inTag = true;
            continue;
        }
        if (c == '>' && inTag) {
            inTag = false;
            continue;
        }
        if (!inTag) out.push_back(c);
    }
    for (const auto& [entity, value] : {std::pair{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&#039;", "'"}, {"&nbsp;", " "}}) {
        ReplaceAll(out, entity, value);
    }
    std::string collapsed;
    bool space = false;
    for (const char c : out) {
        const bool isSpace = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (isSpace) {
            space = !collapsed.empty();
            continue;
        }
        if (space) collapsed.push_back(' ');
        space = false;
        collapsed.push_back(c);
    }
    return collapsed;
}

inline size_t CodePoints(std::string_view text) {
    size_t count = 0;
    for (const char c : text) count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return count;
}

// The first `count` code points.
inline std::string Prefix(std::string_view text, size_t count) {
    size_t i = 0;
    for (size_t seen = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            if (seen == count) break;
            ++seen;
        }
    }
    return std::string(text.substr(0, i));
}

inline bool IsAsciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// A short code is kept; a long one gives way to the initials of the course's name:
// "CSGE602022 - Pemrograman Berbasis Platform (A)" is "PBP".
inline std::string CourseLabel(std::string_view shortName, std::string_view fullName) {
    const std::string shortText = PlainText(shortName);
    if (!shortText.empty() && CodePoints(shortText) <= 10) return shortText;
    std::string name = PlainText(fullName.empty() ? shortName : fullName);
    size_t dash = name.find(" - ");
    if (dash != std::string::npos) {
        const std::string head = name.substr(0, dash);
        const bool code = head.find(' ') == std::string::npos && std::any_of(head.begin(), head.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (code) name = name.substr(dash + 3);
    }
    if (!name.empty() && name.back() == ')') {
        const size_t open = name.rfind('(');
        if (open != std::string::npos) name.resize(open);
    }
    std::string initials;
    size_t start = 0;
    while (start < name.size()) {
        size_t end = name.find(' ', start);
        if (end == std::string::npos) end = name.size();
        std::string word = name.substr(start, end - start);
        std::string lower = word;
        for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        const bool small = lower == "dan" || lower == "and" || lower == "of" || lower == "the" || lower == "untuk" || lower == "di" || lower == "ke";
        if (!word.empty() && !small && IsAsciiLetter(word[0])) initials.push_back(static_cast<char>(word[0] >= 'a' && word[0] <= 'z' ? word[0] - 'a' + 'A' : word[0]));
        start = end + 1;
    }
    if (initials.size() >= 2) return initials.substr(0, 5);
    return Prefix(shortText.empty() ? name : shortText, 10);
}

}  // namespace task_text

namespace moodle_json {

// A small tree of the JSON value, enough for Moodle's answers. Depth is limited, so a hostile answer
// can't recurse without end.
struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    double number = 0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> fields;

    const Value* Get(std::string_view key) const {
        for (const auto& field : fields) {
            if (field.first == key) return &field.second;
        }
        return nullptr;
    }
    std::optional<double> Number(std::string_view key) const {
        const Value* value = Get(key);
        if (!value) return std::nullopt;
        if (value->kind == Kind::Number) return value->number;
        if (value->kind == Kind::String && !value->text.empty()) {
            char* end = nullptr;
            const double parsed = std::strtod(value->text.c_str(), &end);
            if (end && *end == '\0') return parsed;
        }
        return std::nullopt;
    }
    std::string String(std::string_view key) const {
        const Value* value = Get(key);
        return value && value->kind == Kind::String ? value->text : std::string();
    }
};

inline bool Parse(json::Reader& reader, Value& out, int depth = 0) {
    if (depth > 24) return false;
    switch (reader.Peek()) {
        case '"': out.kind = Value::Kind::String; return reader.ReadString(out.text);
        case 't': out.kind = Value::Kind::Bool; out.number = 1; return reader.ConsumeWord("true");
        case 'f': out.kind = Value::Kind::Bool; return reader.ConsumeWord("false");
        case 'n': out.kind = Value::Kind::Null; return reader.ConsumeWord("null");
        case '[':
            out.kind = Value::Kind::Array;
            reader.Consume('[');
            if (reader.Consume(']')) return true;
            do {
                out.items.emplace_back();
                if (!Parse(reader, out.items.back(), depth + 1)) return false;
            } while (reader.Consume(','));
            return reader.Consume(']');
        case '{': {
            out.kind = Value::Kind::Object;
            reader.Consume('{');
            if (reader.Consume('}')) return true;
            do {
                std::string key;
                if (!reader.ReadString(key) || !reader.Consume(':')) return false;
                out.fields.emplace_back(std::move(key), Value{});
                if (!Parse(reader, out.fields.back().second, depth + 1)) return false;
            } while (reader.Consume(','));
            return reader.Consume('}');
        }
        default: out.kind = Value::Kind::Number; return reader.ReadNumber(out.number);
    }
}

inline std::optional<Value> ParseObject(std::string_view text) {
    json::Reader reader(text);
    Value value;
    if (reader.Peek() != '{' || !Parse(reader, value)) return std::nullopt;
    return value;
}

// Moodle reports errors with status 200 and an exception object.
inline MoodleProblem ProblemIn(const Value& object) {
    if (!object.Get("exception") && !object.Get("errorcode")) return MoodleProblem::None;
    const std::string code = object.String("errorcode");
    if (code == "invalidtoken" || code == "accessexception") return MoodleProblem::SignedOut;
    if (code == "sitemaintenance") return MoodleProblem::Maintenance;
    return MoodleProblem::Unavailable;
}

}  // namespace moodle_json

// The answer to login/token.php. `message` explains a NoToken refusal.
inline MoodleProblem MoodleTokenFrom(std::string_view body, std::string* token, std::string* message) {
    const auto object = moodle_json::ParseObject(body);
    if (!object) return MoodleProblem::Unavailable;
    *token = object->String("token");
    if (!token->empty()) return MoodleProblem::None;
    const std::string code = object->String("errorcode");
    if (code == "invalidlogin") return MoodleProblem::WrongLogin;
    if (code == "sitemaintenance") return MoodleProblem::Maintenance;
    *message = object->String("error");
    if (message->empty()) *message = "The site didn't issue a key.";
    return MoodleProblem::NoToken;
}

// The answer to core_webservice_get_site_info.
inline MoodleProblem MoodleSiteInfoFrom(std::string_view body, MoodleSiteInfo* info) {
    const auto object = moodle_json::ParseObject(body);
    if (!object) return MoodleProblem::Unavailable;
    if (const MoodleProblem problem = moodle_json::ProblemIn(*object); problem != MoodleProblem::None) return problem;
    const moodle_json::Value* name = object->Get("fullname");
    if (!name || name->kind != moodle_json::Value::Kind::String) return MoodleProblem::Unavailable;
    info->userName = name->text;
    info->siteName = object->String("sitename");
    info->functions.clear();
    if (const moodle_json::Value* functions = object->Get("functions")) {
        for (const auto& function : functions->items) {
            const std::string fn = function.String("name");
            if (!fn.empty()) info->functions.insert(fn);
        }
    }
    return MoodleProblem::None;
}

// The answer to core_calendar_get_action_events_by_timesort: what the Timeline block shows.
inline MoodleProblem MoodleTasksFrom(std::string_view body, std::vector<MoodleTask>* tasks) {
    const auto object = moodle_json::ParseObject(body);
    if (!object) return MoodleProblem::Unavailable;
    if (const MoodleProblem problem = moodle_json::ProblemIn(*object); problem != MoodleProblem::None) return problem;
    const moodle_json::Value* events = object->Get("events");
    if (!events || events->kind != moodle_json::Value::Kind::Array) return MoodleProblem::Unavailable;
    tasks->clear();
    for (const auto& event : events->items) {
        const auto id = event.Number("id");
        auto due = event.Number("timesort");
        if (!due) due = event.Number("timestart");
        if (!id || !due) continue;
        MoodleTask task;
        task.id = static_cast<int64_t>(*id);
        task.due = *due;
        std::string title = event.String("activityname");
        if (title.empty()) title = task_text::ActivityTitle(event.String("name"));
        task.title = task_text::PlainText(title);
        const moodle_json::Value* course = event.Get("course");
        task.course = task_text::CourseLabel(course ? course->String("shortname") : "", course ? course->String("fullname") : "");
        task.module = TaskModuleFromName(event.String("modulename"));
        task.url = event.String("url");
        if (task.url.empty()) {
            if (const moodle_json::Value* action = event.Get("action")) task.url = action->String("url");
        }
        tasks->push_back(std::move(task));
    }
    return MoodleProblem::None;
}

// Which kinds of task the island tracks.
enum class TaskScope { AssignmentsAndQuizzes, Everything };

struct TaskRule {
    // Overdue work stays listed this long, so a missed deadline doesn't simply vanish.
    static constexpr double kOverdueKept = 7 * 86400.0;
    // The page's load bars show this many days, from today.
    static constexpr int kLoadDays = 7;
    static constexpr int kVisibleRows = 3;
    static constexpr double kDay = 86400;

    static bool Includes(TaskScope scope, TaskModule module) {
        return scope == TaskScope::Everything || module == TaskModule::Assignment || module == TaskModule::Quiz;
    }

    // Open tasks of the chosen kinds, soonest first, without work more than a week overdue.
    static std::vector<MoodleTask> Visible(const std::vector<MoodleTask>& tasks, TaskScope scope, double now) {
        std::vector<MoodleTask> out;
        for (const MoodleTask& task : tasks) {
            if (Includes(scope, task.module) && task.due >= now - kOverdueKept) out.push_back(task);
        }
        std::sort(out.begin(), out.end(), [](const MoodleTask& a, const MoodleTask& b) { return a.due == b.due ? a.id < b.id : a.due < b.due; });
        return out;
    }

    // Tasks that weren't there before and are still ahead.
    static std::vector<MoodleTask> NewTasks(const std::vector<MoodleTask>& tasks, const std::set<int64_t>& seen, double now) {
        std::vector<MoodleTask> out;
        for (const MoodleTask& task : tasks) {
            if (!seen.count(task.id) && task.due > now) out.push_back(task);
        }
        return out;
    }

    static Severity Urgency(double due, double now) {
        const double left = due - now;
        if (left < kDay) return Severity::Critical;
        if (left < 3 * kDay) return Severity::Watch;
        return Severity::Calm;
    }

    // Local times come from the UTC offset in force, in seconds east of UTC.
    struct LocalTime {
        int64_t day;  // Days since 1970 in local time.
        int hour;
        int minute;
        int weekday;  // 0 is Sunday.
        int dayOfMonth;
        int month;  // 0 is January.
    };

    static LocalTime Local(double time, double offset) {
        const auto seconds = static_cast<int64_t>(std::floor(time + offset));
        int64_t day = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
        const int64_t rest = seconds - day * 86400;
        // Civil date from days, after Howard Hinnant.
        const int64_t z = day + 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const int64_t doe = z - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        const int dayOfMonth = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        const int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9) - 1;
        const int weekday = static_cast<int>(((day % 7) + 11) % 7);  // 1970-01-01 was a Thursday.
        return {day, static_cast<int>(rest / 3600), static_cast<int>(rest % 3600 / 60), weekday, dayOfMonth, month};
    }

    static constexpr const char* kWeekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // "2d", "3h", "1d 4h".
    static std::string Span(double seconds, int parts) {
        std::string out;
        for (const auto& [value, unit] : Countdown(seconds, parts)) {
            if (!out.empty()) out += ' ';
            out += std::to_string(value) + unit;
        }
        return out;
    }

    // In its largest units: "1d 2h", "5h 20m", "45m"; under a minute is "0m".
    static std::vector<std::pair<int, const char*>> Countdown(double seconds, int parts = 2) {
        const auto minutes = static_cast<int64_t>(std::fabs(seconds) / 60);
        const std::pair<int64_t, const char*> all[3] = {{minutes / 1440, "d"}, {minutes % 1440 / 60, "h"}, {minutes % 60, "m"}};
        int first = 0;
        while (first < 3 && all[first].first == 0) ++first;
        if (first == 3) return {{0, "m"}};
        std::vector<std::pair<int, const char*>> out;
        for (int i = first; i < 3 && static_cast<int>(out.size()) < parts; ++i) {
            if (all[i].first > 0 || i == first) out.emplace_back(static_cast<int>(all[i].first), all[i].second);
        }
        return out;
    }

    // "Overdue 2d", "Today 23:55", "Tomorrow 10:00", "Fri 10:00", "14 Nov".
    static std::string DueText(double due, double now, double offset) {
        if (due < now) return "Overdue " + Span(now - due, 1);
        const LocalTime when = Local(due, offset);
        const int64_t days = when.day - Local(now, offset).day;
        char time[8];
        std::snprintf(time, sizeof(time), "%02d:%02d", when.hour, when.minute);
        if (days == 0) return std::string("Today ") + time;
        if (days == 1) return std::string("Tomorrow ") + time;
        if (days <= 6) return std::string(kWeekdays[when.weekday]) + " " + time;
        return std::to_string(when.dayOfMonth) + " " + kMonths[when.month];
    }

    // "Thu 13 Nov, 23:55".
    static std::string FullDue(double due, double offset) {
        const LocalTime when = Local(due, offset);
        char text[32];
        std::snprintf(text, sizeof(text), "%s %d %s, %02d:%02d", kWeekdays[when.weekday], when.dayOfMonth, kMonths[when.month], when.hour, when.minute);
        return text;
    }

    // Local days from today to the due day: 0 for today, 1 for tomorrow, negative before today.
    static int DayOffset(double due, double now, double offset) { return static_cast<int>(Local(due, offset).day - Local(now, offset).day); }

    static std::string ReminderKey(const MoodleTask& task, double lead) {
        return std::to_string(task.id) + "@" + std::to_string(static_cast<int64_t>(lead));
    }

    static std::optional<double> NextReminder(const std::vector<MoodleTask>& tasks, const std::vector<double>& leads,
                                              const std::set<std::string>& reminded, double now) {
        std::optional<double> next;
        for (const MoodleTask& task : tasks) {
            for (const double lead : leads) {
                const double time = task.due - lead;
                if (time > now && !reminded.count(ReminderKey(task, lead)) && (!next || time < *next)) next = time;
            }
        }
        return next;
    }

    // Reminders whose time came within `grace`, for tasks still ahead. A PC that slept through one
    // doesn't fire it hours late.
    static std::vector<std::pair<MoodleTask, double>> DueReminders(const std::vector<MoodleTask>& tasks, const std::vector<double>& leads,
                                                                   const std::set<std::string>& reminded, double now, double grace = 600) {
        std::vector<std::pair<MoodleTask, double>> out;
        for (const MoodleTask& task : tasks) {
            for (const double lead : leads) {
                const double time = task.due - lead;
                if (!reminded.count(ReminderKey(task, lead)) && time <= now + 1 && time > now - grace && task.due > now) out.emplace_back(task, lead);
            }
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first.due < b.first.due; });
        return out;
    }
};

// The last list and the tasks already announced, kept on disk so a restart neither empties the page
// nor announces everything again. Same format as macOS.
struct TaskCacheRecord {
    double asOf = 0;
    std::vector<MoodleTask> tasks;
    // Announced, or already there at sign-in: never announced again.
    std::set<int64_t> seen;
    // Announced but not yet looked at on the page.
    std::set<int64_t> unviewed;

    std::string Encode() const {
        const auto clean = [](std::string text) {
            for (char& c : text) {
                if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            }
            return text;
        };
        char number[32];
        std::snprintf(number, sizeof(number), "%.17g", asOf);
        std::string out = std::string("asof ") + number + "\nseen";
        for (const int64_t id : seen) out += " " + std::to_string(id);
        out += "\nunviewed";
        for (const int64_t id : unviewed) out += " " + std::to_string(id);
        for (const MoodleTask& task : tasks) {
            std::snprintf(number, sizeof(number), "%.17g", task.due);
            out += "\n" + std::to_string(task.id) + "\t" + number + "\t" + TaskModuleKey(task.module) + "\t" + clean(task.course) + "\t" + clean(task.title) + "\t" + clean(task.url);
        }
        return out;
    }

    static std::optional<TaskCacheRecord> Decode(std::string_view text) {
        TaskCacheRecord record;
        bool valid = false;
        const auto ids = [](std::string_view rest, std::set<int64_t>& into) {
            size_t start = 0;
            while (start < rest.size()) {
                size_t end = rest.find(' ', start);
                if (end == std::string_view::npos) end = rest.size();
                if (end > start) into.insert(std::strtoll(std::string(rest.substr(start, end - start)).c_str(), nullptr, 10));
                start = end + 1;
            }
        };
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos) end = text.size();
            const std::string_view line = text.substr(start, end - start);
            start = end + 1;
            if (line.substr(0, 5) == "asof ") {
                char* parsed = nullptr;
                const std::string value(line.substr(5));
                record.asOf = std::strtod(value.c_str(), &parsed);
                if (!parsed || *parsed != '\0' || value.empty()) return std::nullopt;
                valid = true;
            } else if (line.substr(0, 8) == "unviewed") {
                ids(line.substr(8), record.unviewed);
            } else if (line.substr(0, 4) == "seen") {
                ids(line.substr(4), record.seen);
            } else {
                std::vector<std::string_view> fields;
                size_t from = 0;
                for (;;) {
                    const size_t tab = line.find('\t', from);
                    fields.push_back(line.substr(from, tab == std::string_view::npos ? std::string_view::npos : tab - from));
                    if (tab == std::string_view::npos) break;
                    from = tab + 1;
                }
                if (fields.size() != 6 || fields[0].empty()) continue;
                MoodleTask task;
                task.id = std::strtoll(std::string(fields[0]).c_str(), nullptr, 10);
                task.due = std::strtod(std::string(fields[1]).c_str(), nullptr);
                task.module = TaskModuleFromKey(fields[2]);
                task.course = std::string(fields[3]);
                task.title = std::string(fields[4]);
                task.url = std::string(fields[5]);
                record.tasks.push_back(std::move(task));
            }
        }
        if (!valid) return std::nullopt;
        return record;
    }
};

// What the tasks page shows, from what the watch knows.
struct TasksPageModel {
    struct Row {
        int64_t id = 0;
        std::string title;
        std::string course;
        std::string due;
        Severity severity = Severity::Calm;
        TaskModule module = TaskModule::Other;
        bool isNew = false;
        std::string url;
        // The day's bar, or -1 past the next seven days.
        int day = -1;
        bool operator==(const Row&) const = default;
    };
    // One of the next seven days, as a bar as tall as its share of the work.
    struct DayLoad {
        std::string letter;
        int count = 0;
        // Something due within a day falls on it.
        bool hasUrgent = false;
        bool hasNew = false;
        bool isBusiest = false;
        bool operator==(const DayLoad&) const = default;
    };

    // A short message in the middle, with a button when `button` is set: it opens settings, or shows
    // overdue work when `buttonShowsOverdue`.
    bool isMessage = true;
    std::string title;
    std::string detail;
    std::string button;
    bool buttonShowsOverdue = false;
    bool allClear = false;
    // "Show 2 overdue" or "Hide overdue" beside the list, or empty when nothing is overdue.
    std::string overdueToggle;
    // How many tasks the list scrolls through.
    int listCount = 0;

    // The list.
    std::string heroLabel;
    std::vector<std::pair<int, const char*>> countdown;
    std::string heroTitle;
    std::string heroCourse;
    std::string heroDue;
    Severity heroSeverity = Severity::Calm;
    std::string heroUrl;
    std::vector<DayLoad> load;
    // Beside the bars: "Busiest: Thu · 3" over "5 due in 7 days".
    std::string loadCaption;
    std::string loadDetail;
    std::vector<Row> rows;
    std::string more;
    // "Scele", "EMAS3" or "Moodle", for the button and messages.
    std::string siteName = "Scele";

    bool operator==(const TasksPageModel& o) const {
        if (isMessage != o.isMessage || title != o.title || detail != o.detail || button != o.button || allClear != o.allClear) return false;
        if (buttonShowsOverdue != o.buttonShowsOverdue || overdueToggle != o.overdueToggle || listCount != o.listCount) return false;
        if (heroLabel != o.heroLabel || heroTitle != o.heroTitle || heroCourse != o.heroCourse || heroDue != o.heroDue) return false;
        if (heroSeverity != o.heroSeverity || heroUrl != o.heroUrl || load != o.load || rows != o.rows || more != o.more) return false;
        if (loadCaption != o.loadCaption || loadDetail != o.loadDetail || siteName != o.siteName) return false;
        if (countdown.size() != o.countdown.size()) return false;
        for (size_t i = 0; i < countdown.size(); ++i) {
            if (countdown[i].first != o.countdown[i].first || std::string_view(countdown[i].second) != o.countdown[i].second) return false;
        }
        return true;
    }

    // Overdue work is hidden until `showOverdue`: the page is about what can still be done.
    static TasksPageModel Make(const std::vector<MoodleTask>& allTasks, const std::set<int64_t>& unviewed, bool signedIn, bool checking,
                               MoodleProblem problem, std::optional<double> asOf, double now, int offset, double utcOffset, bool showOverdue = false,
                               const std::string& site = "Scele") {
        TasksPageModel model;
        model.siteName = site;
        const auto clock = [utcOffset](double time) {
            const TaskRule::LocalTime local = TaskRule::Local(time, utcOffset);
            char text[8];
            std::snprintf(text, sizeof(text), "%02d:%02d", local.hour, local.minute);
            return std::string(text);
        };
        if (!signedIn) {
            model.title = "See your " + site + " tasks here";
            model.detail = "Sign in once in Settings. Your password isn't stored.";
            model.button = "Sign in\xE2\x80\xA6";
            return model;
        }
        if (problem == MoodleProblem::SignedOut) {
            model.title = "Signed out of " + site;
            model.detail = "The key was refused. Sign in again in Settings.";
            model.button = "Sign in\xE2\x80\xA6";
            return model;
        }
        if (!asOf) {
            const bool waiting = problem == MoodleProblem::None || checking;
            model.title = waiting ? "Checking " + site + "\xE2\x80\xA6" : problem == MoodleProblem::Maintenance ? site + " is under maintenance" : "Couldn't reach " + site;
            model.detail = checking ? "" : "Noitch will try again soon.";
            return model;
        }
        const int weekday = TaskRule::Local(now, utcOffset).weekday;
        std::vector<MoodleTask> tasks;
        int overdue = 0;
        for (const MoodleTask& task : allTasks) {
            if (task.due < now) ++overdue;
            if (showOverdue || task.due >= now) tasks.push_back(task);
        }
        const std::string reveal = "Show " + std::to_string(overdue) + " overdue";
        if (tasks.empty()) {
            model.title = "All clear";
            model.detail = std::string(overdue > 0 ? "Nothing else due" : "Nothing to do") + (problem == MoodleProblem::None ? "" : " as of " + clock(*asOf)) +
                           " \xC2\xB7 checked " + clock(*asOf);
            model.allClear = true;
            if (overdue > 0) model.button = reveal;
            model.buttonShowsOverdue = true;
            return model;
        }
        model.overdueToggle = overdue == 0 ? "" : showOverdue ? "Hide overdue" : reveal;
        model.listCount = static_cast<int>(tasks.size());
        model.isMessage = false;
        const MoodleTask& first = tasks.front();
        model.heroLabel = std::string(first.due < now ? "Overdue" : "Next due") + (problem != MoodleProblem::None ? " \xC2\xB7 as of " + clock(*asOf) : "");
        model.countdown = TaskRule::Countdown(first.due - now);
        model.heroTitle = first.title;
        model.heroCourse = first.course;
        model.heroDue = TaskRule::FullDue(first.due, utcOffset);
        model.heroSeverity = TaskRule::Urgency(first.due, now);
        model.heroUrl = first.url;
        // The week ahead counts what can still be done, whether or not overdue work is shown.
        const auto loadDay = [&](const MoodleTask& task) {
            const int day = TaskRule::DayOffset(task.due, now, utcOffset);
            return task.due >= now && day >= 0 && day < TaskRule::kLoadDays ? day : -1;
        };
        model.load.resize(TaskRule::kLoadDays);
        for (int day = 0; day < TaskRule::kLoadDays; ++day) model.load[static_cast<size_t>(day)].letter = std::string(1, TaskRule::kWeekdays[(weekday + day) % 7][0]);
        int total = 0;
        for (const MoodleTask& task : allTasks) {
            const int day = loadDay(task);
            if (day < 0) continue;
            DayLoad& load = model.load[static_cast<size_t>(day)];
            ++load.count;
            ++total;
            load.hasUrgent = load.hasUrgent || TaskRule::Urgency(task.due, now) == Severity::Critical;
            load.hasNew = load.hasNew || unviewed.count(task.id) > 0;
        }
        int busiestDay = 0;
        for (int day = 1; day < TaskRule::kLoadDays; ++day) {
            if (model.load[static_cast<size_t>(day)].count > model.load[static_cast<size_t>(busiestDay)].count) busiestDay = day;
        }
        const int busiest = model.load[static_cast<size_t>(busiestDay)].count;
        model.load[static_cast<size_t>(busiestDay)].isBusiest = busiest > 0;
        const std::string busiestName = busiestDay == 0 ? "today" : busiestDay == 1 ? "tomorrow" : TaskRule::kWeekdays[(weekday + busiestDay) % 7];
        model.loadCaption = total == 0 ? "Nothing due" : "Busiest: " + busiestName + " \xC2\xB7 " + std::to_string(busiest);
        model.loadDetail = total == 0 ? "in the next 7 days" : std::to_string(total) + " due in 7 days";
        const int count = static_cast<int>(tasks.size());
        const int start = std::clamp(offset, 0, std::max(count - TaskRule::kVisibleRows, 0));
        for (int i = start; i < std::min(start + TaskRule::kVisibleRows, count); ++i) {
            const MoodleTask& task = tasks[static_cast<size_t>(i)];
            model.rows.push_back({task.id, task.title, task.course, TaskRule::DueText(task.due, now, utcOffset), TaskRule::Urgency(task.due, now), task.module,
                                  unviewed.count(task.id) > 0, task.url, loadDay(task)});
        }
        if (count > TaskRule::kVisibleRows) {
            model.more = start == 0 ? "+" + std::to_string(count - TaskRule::kVisibleRows) + " more \xC2\xB7 scroll"
                                    : std::to_string(start + 1) + "\xE2\x80\x93" + std::to_string(start + static_cast<int>(model.rows.size())) + " of " + std::to_string(count);
        }
        return model;
    }
};

}  // namespace noitch
