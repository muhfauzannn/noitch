#include "settings_store.h"

#include <windows.h>

#include <cstdint>

namespace noitch {

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\Noitch";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Noitch";

bool ReadDword(const wchar_t* name, DWORD& stored) {
    DWORD size = sizeof(stored);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &stored, &size) == ERROR_SUCCESS;
}

void Read(const wchar_t* name, bool& value) {
    DWORD stored = 0;
    if (ReadDword(name, stored)) value = stored != 0;
}

// Fractional values are stored in thousandths, as a signed 32-bit number, and clamped on the way in.
void Read(const wchar_t* name, double& value, double low, double high) {
    DWORD stored = 0;
    if (!ReadDword(name, stored)) return;
    const double read = static_cast<double>(static_cast<int32_t>(stored)) / 1000.0;
    value = read < low ? low : read > high ? high : read;
}

void Write(HKEY key, const wchar_t* name, DWORD stored) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&stored), sizeof(stored));
}

void Write(HKEY key, const wchar_t* name, bool value) { Write(key, name, static_cast<DWORD>(value ? 1 : 0)); }

void Write(HKEY key, const wchar_t* name, double value) {
    Write(key, name, static_cast<DWORD>(static_cast<int32_t>(value * 1000.0 + (value < 0 ? -0.5 : 0.5))));
}

}  // namespace

// Each feature's part reads its own values. The names from before the split are kept, so existing
// choices carry over.
AppSettings LoadSettings() {
    AppSettings settings;
    Read(L"OpenOnHover", settings.general.openOnHover);
    Read(L"HoverDelay", settings.general.hoverDelay, 0, 1);
    Read(L"ShowUsagePage", settings.pages.showUsage);
    DWORD openOn = 0;
    if (ReadDword(L"OpenOn", openOn) && openOn <= static_cast<DWORD>(OpenPage::Usage)) settings.pages.openOn = static_cast<OpenPage>(openOn);
    Read(L"ShowLyrics", settings.home.showLyrics);
    Read(L"ShowNextLyric", settings.home.showNextLyric);
    Read(L"LyricsOffset", settings.home.lyricsOffset, -5, 5);
    Read(L"ShowShuffleAndRepeat", settings.home.showShuffleAndRepeat);
    Read(L"TintWithArtwork", settings.home.tintWithArtwork);
    Read(L"ArtworkGlow", settings.home.artworkGlow);
    Read(L"ShowLive", settings.home.showLive);
    Read(L"UsageShowClaude", settings.usage.showClaude);
    Read(L"UsageShowCodex", settings.usage.showCodex);
    Read(L"UsageWarnAt", settings.usage.warnAt, 30, 95);
    Read(L"UsageRefreshMinutes", settings.usage.refreshMinutes, 1, 60);
    return settings;
}

void SaveSettings(const AppSettings& settings) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    Write(key, L"OpenOnHover", settings.general.openOnHover);
    Write(key, L"HoverDelay", settings.general.hoverDelay);
    Write(key, L"ShowUsagePage", settings.pages.showUsage);
    Write(key, L"OpenOn", static_cast<DWORD>(settings.pages.openOn));
    Write(key, L"ShowLyrics", settings.home.showLyrics);
    Write(key, L"ShowNextLyric", settings.home.showNextLyric);
    Write(key, L"LyricsOffset", settings.home.lyricsOffset);
    Write(key, L"ShowShuffleAndRepeat", settings.home.showShuffleAndRepeat);
    Write(key, L"TintWithArtwork", settings.home.tintWithArtwork);
    Write(key, L"ArtworkGlow", settings.home.artworkGlow);
    Write(key, L"ShowLive", settings.home.showLive);
    Write(key, L"UsageShowClaude", settings.usage.showClaude);
    Write(key, L"UsageShowCodex", settings.usage.showCodex);
    Write(key, L"UsageWarnAt", settings.usage.warnAt);
    Write(key, L"UsageRefreshMinutes", settings.usage.refreshMinutes);
    RegCloseKey(key);
}

bool LaunchesAtLogin() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

bool SetLaunchesAtLogin(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS status;
    if (enabled) {
        wchar_t path[MAX_PATH + 2] = L"\"";
        const DWORD length = GetModuleFileNameW(nullptr, path + 1, MAX_PATH);
        path[length + 1] = L'"';
        path[length + 2] = L'\0';
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), (length + 3) * sizeof(wchar_t));
    } else {
        status = RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS || (!enabled && status == ERROR_FILE_NOT_FOUND);
}

}  // namespace noitch
