#include "usage_controller.h"

#include <shlobj.h>

#include <cmath>
#include <string>

namespace noitch {

namespace {

// A quick return to the page shouldn't pay for a new surface and a fresh read of every log.
constexpr UINT kReleaseDelayMs = 10000;

}  // namespace

void UsageController::PageWillShow(Painter& painter, const UsageLayout& layout, float left, float scale) {
    KillTimer(window_, kReleaseTimer);
    // Show what was last known at once, rather than an empty page while asking.
    if (claudeLimits_.problem == LimitProblem::Loading) {
        if (const auto known = Remembered("claude")) claudeLimits_ = *known;
    }
    if (!scene_.Exists() && FAILED(scene_.Create(*compositor_, painter, layout, left, scale))) scene_.Release();
    Refresh();
}

void UsageController::PageWillHide() {
    KillTimer(window_, kTickTimer);
    ++claudeGeneration_;
    // One-shot, not periodic: it fires once, unless the page is shown again first.
    SetTimer(window_, kReleaseTimer, kReleaseDelayMs, nullptr);
}

void UsageController::DeviceLost() {
    // The surface went with the old device. The next showing makes a new one.
    scene_.Release();
}

void UsageController::Release() {
    KillTimer(window_, kReleaseTimer);
    KillTimer(window_, kTickTimer);
    scene_.Release();
    source_.reset();
    claudeLimits_ = ToolLimits::Failed(LimitProblem::Loading);
    loadedCache_ = false;
    rememberedClaude_.reset();
    rememberedCodex_.reset();
}

namespace {

std::wstring CacheFile() {
    wchar_t* base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) return {};
    std::wstring folder = std::wstring(base) + L"\\Noitch";
    CoTaskMemFree(base);
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder + L"\\limits";
}

}  // namespace

std::optional<ToolLimits> UsageController::Remembered(const char* tool) {
    if (!loadedCache_) {
        loadedCache_ = true;
        std::string text;
        const std::wstring path = CacheFile();
        const HANDLE file = path.empty() ? INVALID_HANDLE_VALUE : CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            char buffer[1024];
            DWORD read = 0;
            if (ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) text.assign(buffer, read);
            CloseHandle(file);
        }
        rememberedClaude_ = DecodeLimitCache(text, "claude");
        rememberedCodex_ = DecodeLimitCache(text, "codex");
    }
    return std::string_view(tool) == "claude" ? rememberedClaude_ : rememberedCodex_;
}

// Written only when the numbers change or have aged ten minutes, and only while the page is shown.
void UsageController::Remember(const char* tool, const ToolLimits& limits, double asOf) {
    Remembered(tool);
    std::optional<ToolLimits>& slot = std::string_view(tool) == "claude" ? rememberedClaude_ : rememberedCodex_;
    if (slot && slot->fiveHour == limits.fiveHour && slot->week == limits.week && asOf - slot->asOf.value_or(0) <= 600) return;
    ToolLimits stored;
    stored.fiveHour = limits.fiveHour;
    stored.week = limits.week;
    stored.asOf = asOf;
    slot = stored;
    std::string text;
    if (rememberedClaude_) text += EncodeLimitCache("claude", *rememberedClaude_, *rememberedClaude_->asOf) + "\n";
    if (rememberedCodex_) text += EncodeLimitCache("codex", *rememberedCodex_, *rememberedCodex_->asOf) + "\n";
    const std::wstring path = CacheFile();
    if (path.empty()) return;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

void UsageController::Refresh() {
    if (!scene_.Exists()) return;
    if (!source_) source_ = std::make_unique<UsageSource>();
    const double now = UnixNow();
    source_->Refresh(now);
    Render();
    ClaudeLimitSource::Fetch(window_, ++claudeGeneration_, now);
    // One shot at the next minute, only while shown: the countdowns are in minutes, and new log
    // lines and limits are picked up on the same look.
    const double delay = 60 - std::fmod(now, 60) + 0.5;
    SetTimer(window_, kTickTimer, static_cast<UINT>(delay * 1000), nullptr);
}

// Combines the latest limits with the local logs. Cheap: no reading, no request.
void UsageController::Render() {
    if (!scene_.Exists() || !source_) return;
    const double now = UnixNow();
    const UsageLog& log = source_->Log();
    ToolLimits claude = claudeLimits_;
    if (claude.fiveHour) claude.recentRate = ClaudeRate(log.Entries(), *claude.fiveHour, now);
    ToolLimits codex = ToolLimits::Failed(LimitProblem::NoData);
    if (!log.CodexSamples().empty()) {
        codex = ToolLimits{};
        codex.fiveHour = log.CodexSamples().back().fiveHour;
        codex.week = log.CodexSamples().back().week;
        codex.recentRate = CodexRate(log.CodexSamples(), now);
        Remember("codex", codex, log.CodexSamples().back().time);
    } else if (const auto known = Remembered("codex")) {
        // The logs that held them are over a day old; the week may still be running.
        codex = *known;
    }
    scene_.Paint(MakeUsageModel(claude, codex, Summarize(log, now, LocalDayStart()), now, LocalClock));
}

bool UsageController::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == ClaudeLimitSource::kMessage) {
        std::unique_ptr<ToolLimits> limits(reinterpret_cast<ToolLimits*>(lparam));
        if (wparam == claudeGeneration_ && limits) {
            if (limits->problem == LimitProblem::None) {
                claudeLimits_ = *limits;
                Remember("claude", *limits, UnixNow());
            } else if ((limits->problem == LimitProblem::Unavailable || limits->problem == LimitProblem::Expired) &&
                       Remembered("claude")) {
                // Offline, refused or logged out for now: the last good numbers beat a blank page. An
                // expired login still says so, because only the user can renew it.
                claudeLimits_ = *Remembered("claude");
                if (limits->problem == LimitProblem::Expired) claudeLimits_.problem = LimitProblem::Expired;
            } else {
                claudeLimits_ = *limits;
            }
            Render();
        }
        return true;
    }
    if (message != WM_TIMER) return false;
    if (wparam == kTickTimer) {
        KillTimer(window_, kTickTimer);
        Refresh();
        return true;
    }
    if (wparam == kReleaseTimer) {
        Release();
        return true;
    }
    return false;
}

}  // namespace noitch
