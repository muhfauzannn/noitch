#pragma once

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "ai_usage.h"

namespace noitch {

// Reads Claude Code's and Codex's local logs. There is no event cheaper than a look, so it reads only
// when asked: when the page is shown, and once a minute while it stays shown. Each look reads only
// bytes appended since the last one, from a mapped view. Nothing leaves the machine.
class UsageSource {
public:
    // The block estimate can reach back further than today, so a day is kept.
    static constexpr double kWindow = 24 * 3600;

    UsageSource();
    const UsageLog& Refresh(double now);

private:
    struct FileState {
        uint64_t offset = 0;
        CodexFileState codex;
        bool seen = false;
    };

    void Scan(const std::wstring& directory, AITool tool, double cutoff);
    void ReadLog(const std::wstring& path, uint64_t size, AITool tool, double cutoff);

    std::vector<std::wstring> claudeRoots_;
    std::vector<std::wstring> codexRoots_;
    UsageLog log_;
    std::unordered_map<std::wstring, FileState> files_;
};

// Seconds since 1970, and the start of today in local time, as UsageSummary wants them.
double UnixNow();
double LocalDayStart();

}  // namespace noitch
