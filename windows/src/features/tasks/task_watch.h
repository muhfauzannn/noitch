#pragma once

#include <windows.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "moodle_source.h"
#include "task_model.h"
#include "tasks_settings.h"

namespace noitch {

// Owns what is known about the Moodle tasks, whether or not the island is open: the list, which tasks
// are new, and when to ask again. Signed out, or with the page turned off, it holds nothing and
// schedules nothing. Matches TaskWatch.swift.
//
// Checks happen on these events only: the island opening or the page showing (at most every 10
// minutes, or 2 for the page), the PC resuming, and one one-shot coalescable timer per check
// interval, re-armed after each check. Reminders are one one-shot timer at the next reminder time.
class TaskWatch {
public:
    static constexpr UINT_PTR kCheckTimer = 13;
    static constexpr UINT_PTR kReminderTimer = 14;

    void Initialize(HWND window, const TasksSettings& settings, bool pageOn);
    void Apply(const TasksSettings& settings, bool pageOn);
    void IslandWillOpen() { CheckIfOlder(kOpenThrottle); }
    void PageWillShow() { CheckIfOlder(kPageThrottle); }
    // The page has been looked at, so nothing is new any more.
    void MarkViewed();

    // True for a message the watch owns: its timers, its answers and resuming from sleep.
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    // Soonest first, of the chosen kinds.
    std::vector<MoodleTask> Tasks() const;
    const std::set<int64_t>& Unviewed() const { return record_ ? record_->unviewed : empty_; }
    const TasksSettings& Settings() const { return settings_; }
    std::optional<double> AsOf() const { return record_ ? std::optional<double>(record_->asOf) : std::nullopt; }
    MoodleProblem Problem() const { return problem_; }
    bool IsChecking() const { return checking_; }

    // The list, the problem or the checking state changed.
    void (*onChange)(void* context) = nullptr;
    // Tasks seen for the first time, to announce on the closed island.
    void (*onNew)(void* context, const std::vector<MoodleTask>& tasks) = nullptr;
    void (*onReminder)(void* context, const MoodleTask& task, double lead) = nullptr;
    void (*onUnviewedChange)(void* context) = nullptr;
    void* context = nullptr;

private:
    static constexpr double kOpenThrottle = 600;
    static constexpr double kPageThrottle = 120;
    static constexpr double kLaunchDelay = 3;

    bool Running() const { return settings_.IsSignedIn() && pageOn_; }
    void Start();
    void Stop(bool forget);
    void CheckIfOlder(double seconds);
    void Check();
    void Finish(MoodleSource::Result& result);
    void Save() const;
    void ScheduleCheck();
    void Arm(double delay);
    void ScheduleReminder();
    void Remind();
    void Changed() const {
        if (onChange) onChange(context);
    }
    void UnviewedChanged() const {
        if (onUnviewedChange) onUnviewedChange(context);
    }

    HWND window_ = nullptr;
    TasksSettings settings_;
    bool pageOn_ = true;
    std::optional<TaskCacheRecord> record_;
    MoodleProblem problem_ = MoodleProblem::None;
    bool checking_ = false;
    // A late answer to a request that was cancelled is dropped.
    uintptr_t generation_ = 0;
    double lastCheck_ = 0;
    std::set<std::string> reminded_;
    std::set<int64_t> empty_;
};

// Seconds since 1970, and the local offset from UTC in force now.
double TaskNow();
double LocalUtcOffset();

}  // namespace noitch
