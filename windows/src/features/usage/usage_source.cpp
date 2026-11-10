#include "usage_source.h"

#include "ai_limits.h"

namespace noitch {

namespace {

constexpr uint64_t kUnixEpochTicks = 116444736000000000ULL;

double FromFileTime(const FILETIME& time) {
    const uint64_t ticks = (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    return static_cast<double>(static_cast<int64_t>(ticks - kUnixEpochTicks)) / 1e7;
}

std::wstring Environment(const wchar_t* name) {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    return length > 0 && length < MAX_PATH ? std::wstring(buffer, length) : std::wstring();
}

}  // namespace

double UnixNow() {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return FromFileTime(now);
}

double LocalDayStart() {
    SYSTEMTIME local;
    GetLocalTime(&local);
    local.wHour = local.wMinute = local.wSecond = local.wMilliseconds = 0;
    SYSTEMTIME utc;
    FILETIME start;
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &start)) return UnixNow() - 86400;
    return FromFileTime(start);
}

std::string LocalClock(double time) {
    const uint64_t ticks = static_cast<uint64_t>(time * 1e7) + kUnixEpochTicks;
    const FILETIME utc{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME universal;
    SYSTEMTIME local;
    if (!FileTimeToSystemTime(&utc, &universal) || !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local)) return {};
    wchar_t day[16];
    wchar_t clock[16];
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"ddd", day, 16, nullptr) ||
        !GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS | TIME_FORCE24HOURFORMAT, &local, nullptr, clock, 16)) return {};
    const std::wstring text = std::wstring(day) + L" " + clock;
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

UsageSource::UsageSource() {
    const std::wstring home = Environment(L"USERPROFILE");
    const std::wstring claude = Environment(L"CLAUDE_CONFIG_DIR");
    if (!claude.empty()) {
        size_t start = 0;
        while (start <= claude.size()) {
            const size_t comma = claude.find(L',', start);
            const std::wstring root = claude.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            if (!root.empty()) claudeRoots_.push_back(root + L"\\projects");
            if (comma == std::wstring::npos) break;
            start = comma + 1;
        }
    } else if (!home.empty()) {
        claudeRoots_.push_back(home + L"\\.config\\claude\\projects");
        claudeRoots_.push_back(home + L"\\.claude\\projects");
    }
    const std::wstring codex = Environment(L"CODEX_HOME");
    if (!codex.empty()) {
        codexRoots_.push_back(codex + L"\\sessions");
    } else if (!home.empty()) {
        codexRoots_.push_back(home + L"\\.codex\\sessions");
    }
}

const UsageLog& UsageSource::Refresh(double now) {
    const double cutoff = now - kWindow;
    for (auto& entry : files_) entry.second.seen = false;
    for (const auto& root : claudeRoots_) Scan(root, AITool::Claude, cutoff);
    for (const auto& root : codexRoots_) Scan(root, AITool::Codex, cutoff);
    // A file that has gone quiet for a day can't add anything inside the window.
    for (auto it = files_.begin(); it != files_.end();) {
        it = it->second.seen ? std::next(it) : files_.erase(it);
    }
    log_.Prune(cutoff, now - 2 * kRecentSpan);
    return log_;
}

void UsageSource::Scan(const std::wstring& directory, AITool tool, double cutoff) {
    WIN32_FIND_DATAW found;
    const HANDLE find = FindFirstFileExW((directory + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = directory + L"\\" + name;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) Scan(path, tool, cutoff);
            continue;
        }
        if (name.size() < 6 || name.compare(name.size() - 6, 6, L".jsonl") != 0) continue;
        if (FromFileTime(found.ftLastWriteTime) < cutoff) continue;
        const uint64_t size = (static_cast<uint64_t>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
        ReadLog(path, size, tool, cutoff);
    } while (FindNextFileW(find, &found));
    FindClose(find);
}

void UsageSource::ReadLog(const std::wstring& path, uint64_t size, AITool tool, double cutoff) {
    FileState& state = files_[path];
    state.seen = true;
    if (size < state.offset) state = FileState{0, {}, true};
    if (size == state.offset) return;
    // The tools keep writing while we read, so share everything.
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    // Mapped, so a large first read never becomes private memory.
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, static_cast<DWORD>(size >> 32), static_cast<DWORD>(size), nullptr);
    if (mapping) {
        if (const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(size))) {
            const std::string_view rest(static_cast<const char*>(view) + state.offset, static_cast<size_t>(size - state.offset));
            state.offset += tool == AITool::Claude ? log_.ReadClaude(rest, cutoff) : log_.ReadCodex(rest, state.codex, cutoff);
            UnmapViewOfFile(view);
        }
        CloseHandle(mapping);
    }
    CloseHandle(file);
}

}  // namespace noitch
