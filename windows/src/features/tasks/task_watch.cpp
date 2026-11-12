#include "task_watch.h"

#include <shlobj.h>

#include <algorithm>
#include <bit>
#include <memory>

namespace noitch {

namespace {

std::wstring CacheFile() {
    wchar_t* base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) return {};
    std::wstring folder = std::wstring(base) + L"\\Noitch";
    CoTaskMemFree(base);
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder + L"\\tasks";
}

}  // namespace

double TaskNow() {
    FILETIME file;
    GetSystemTimeAsFileTime(&file);
    const ULONGLONG ticks = (static_cast<ULONGLONG>(file.dwHighDateTime) << 32) | file.dwLowDateTime;
    return static_cast<double>(ticks - 116444736000000000ULL) / 1e7;
}

double LocalUtcOffset() {
    TIME_ZONE_INFORMATION zone;
    const DWORD mode = GetTimeZoneInformation(&zone);
    LONG bias = zone.Bias;
    if (mode == TIME_ZONE_ID_DAYLIGHT) bias += zone.DaylightBias;
    if (mode == TIME_ZONE_ID_STANDARD) bias += zone.StandardBias;
    return -bias * 60.0;
}

void TaskWatch::Initialize(HWND window, const TasksSettings& settings, bool pageOn) {
    window_ = window;
    settings_ = settings;
    pageOn_ = pageOn;
    Start();
}

std::vector<MoodleTask> TaskWatch::Tasks() const {
    if (!record_) return {};
    return TaskRule::Visible(record_->tasks, settings_.scope, TaskNow());
}

void TaskWatch::Apply(const TasksSettings& settings, bool pageOn) {
    const TasksSettings old = settings_;
    const bool wasOn = pageOn_;
    settings_ = settings;
    pageOn_ = pageOn;
    if (settings.site != old.site || settings.accountName != old.accountName || pageOn != wasOn) {
        Stop(!settings.IsSignedIn() || settings.site != old.site);
        Start();
        Changed();
        UnviewedChanged();
        return;
    }
    if (settings.checkMinutes != old.checkMinutes) ScheduleCheck();
    if (settings.scope != old.scope || settings.reminders != old.reminders) {
        ScheduleReminder();
        Changed();
    }
}

void TaskWatch::MarkViewed() {
    if (!record_ || record_->unviewed.empty()) return;
    record_->unviewed.clear();
    Save();
    UnviewedChanged();
}

void TaskWatch::Start() {
    if (!Running()) return;
    std::string text;
    const std::wstring path = CacheFile();
    const HANDLE file = path.empty() ? INVALID_HANDLE_VALUE : CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size)) {
            const auto byteCount = std::bit_cast<LONGLONG>(size);
            if (byteCount > 0 && byteCount < (1 << 20)) {
                text.resize(static_cast<size_t>(byteCount));
                DWORD read = 0;
                if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) read = 0;
                text.resize(read);
            }
        }
        CloseHandle(file);
    }
    record_ = TaskCacheRecord::Decode(text);
    lastCheck_ = record_ ? record_->asOf : 0;
    const double interval = settings_.checkMinutes * 60;
    if (interval > 0 && (lastCheck_ == 0 || TaskNow() - lastCheck_ >= interval)) {
        Arm(kLaunchDelay);
    } else {
        ScheduleCheck();
    }
    ScheduleReminder();
}

void TaskWatch::Stop(bool forget) {
    ++generation_;
    checking_ = false;
    KillTimer(window_, kCheckTimer);
    KillTimer(window_, kReminderTimer);
    record_.reset();
    problem_ = MoodleProblem::None;
    lastCheck_ = 0;
    reminded_.clear();
    if (forget) {
        const std::wstring path = CacheFile();
        if (!path.empty()) DeleteFileW(path.c_str());
    }
}

void TaskWatch::CheckIfOlder(double seconds) {
    if (!Running() || checking_) return;
    if (lastCheck_ > 0 && TaskNow() - lastCheck_ < seconds) return;
    Check();
}

void TaskWatch::Check() {
    if (!Running() || checking_) return;
    KillTimer(window_, kCheckTimer);
    checking_ = true;
    lastCheck_ = TaskNow();
    Changed();
    MoodleSource::Tasks(window_, ++generation_, settings_.site, lastCheck_);
}

void TaskWatch::Finish(MoodleSource::Result& result) {
    checking_ = false;
    const double now = TaskNow();
    problem_ = result.problem;
    if (result.problem == MoodleProblem::None) {
        std::set<int64_t> ids;
        for (const MoodleTask& task : result.tasks) ids.insert(task.id);
        if (record_) {
            const std::vector<MoodleTask> fresh = TaskRule::NewTasks(TaskRule::Visible(result.tasks, settings_.scope, now), record_->seen, now);
            // Ids that left the list are forgotten, so the file stays as small as the list.
            std::set<int64_t> seen;
            std::set<int64_t> unviewed;
            for (const int64_t id : record_->seen) {
                if (ids.count(id)) seen.insert(id);
            }
            for (const int64_t id : record_->unviewed) {
                if (ids.count(id)) unviewed.insert(id);
            }
            for (const MoodleTask& task : fresh) {
                seen.insert(task.id);
                unviewed.insert(task.id);
            }
            record_->seen = std::move(seen);
            record_->unviewed = std::move(unviewed);
            record_->tasks = std::move(result.tasks);
            record_->asOf = now;
            Save();
            if (!fresh.empty()) {
                if (onNew) onNew(context, fresh);
                UnviewedChanged();
            }
        } else {
            // The first list after signing in is what was already there: nothing to announce.
            TaskCacheRecord record;
            record.asOf = now;
            record.tasks = std::move(result.tasks);
            record.seen = std::move(ids);
            record_ = std::move(record);
            Save();
        }
    }
    // A refused key won't start working by itself; signing in again rearms this.
    if (problem_ != MoodleProblem::SignedOut) ScheduleCheck();
    ScheduleReminder();
    Changed();
}

void TaskWatch::Save() const {
    if (!record_) return;
    const std::string text = record_->Encode();
    const std::wstring path = CacheFile();
    if (path.empty()) return;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

// One shot at the end of the interval since the last check. "Only when I open the island" arms nothing.
void TaskWatch::ScheduleCheck() {
    KillTimer(window_, kCheckTimer);
    const double interval = settings_.checkMinutes * 60;
    if (!Running() || interval <= 0 || checking_) return;
    const double now = TaskNow();
    Arm(std::max((lastCheck_ > 0 ? lastCheck_ : now) + interval - now, 1.0));
}

void TaskWatch::Arm(double delay) {
    // Lets the system fold this wakeup into others; a few minutes late is fine for this.
    const auto tolerance = static_cast<ULONG>(std::min(300.0, delay * 0.2) * 1000);
    SetCoalescableTimer(window_, kCheckTimer, static_cast<UINT>(delay * 1000), nullptr, tolerance);
}

void TaskWatch::ScheduleReminder() {
    KillTimer(window_, kReminderTimer);
    if (!Running()) return;
    const double now = TaskNow();
    const auto next = TaskRule::NextReminder(Tasks(), ReminderLeads(settings_.reminders), reminded_, now);
    if (!next) return;
    // SetTimer holds about 24 days; a later reminder is rearmed by the checks before then.
    const double delay = std::min(std::max(*next - now, 0.5), 20.0 * 86400);
    SetCoalescableTimer(window_, kReminderTimer, static_cast<UINT>(delay * 1000), nullptr, 20000);
}

void TaskWatch::Remind() {
    KillTimer(window_, kReminderTimer);
    const double now = TaskNow();
    const std::vector<double> leads = ReminderLeads(settings_.reminders);
    const std::vector<MoodleTask> tasks = Tasks();
    const auto due = TaskRule::DueReminders(tasks, leads, reminded_, now + 30);
    for (const auto& [task, lead] : due) reminded_.insert(TaskRule::ReminderKey(task, lead));
    // Reminders that fell while asleep are marked too, so they never fire late.
    for (const MoodleTask& task : tasks) {
        for (const double lead : leads) {
            if (task.due - lead <= now) reminded_.insert(TaskRule::ReminderKey(task, lead));
        }
    }
    if (!due.empty() && onReminder) onReminder(context, due.front().first, due.front().second);
    ScheduleReminder();
}

bool TaskWatch::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == MoodleSource::kMessage) {
        std::unique_ptr<MoodleSource::Result> result(reinterpret_cast<MoodleSource::Result*>(lparam));
        // Sign-in answers go to the settings window, so anything else here is stray; it is freed.
        if (!result || result->kind != MoodleSource::Kind::Tasks) return true;
        if (wparam == generation_ && checking_) Finish(*result);
        return true;
    }
    if (message == WM_POWERBROADCAST && wparam == PBT_APMRESUMEAUTOMATIC) {
        CheckIfOlder(kOpenThrottle);
        return false;
    }
    if (message != WM_TIMER) return false;
    if (wparam == kCheckTimer) {
        KillTimer(window_, kCheckTimer);
        Check();
        return true;
    }
    if (wparam == kReminderTimer) {
        Remind();
        return true;
    }
    return false;
}

}  // namespace noitch
