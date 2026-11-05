#include "settings_store.h"

#include <windows.h>

namespace noitch {

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\Noitch";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Noitch";

void Read(const wchar_t* name, bool& value) {
    DWORD stored = 0;
    DWORD size = sizeof(stored);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &stored, &size) == ERROR_SUCCESS) {
        value = stored != 0;
    }
}

void Write(HKEY key, const wchar_t* name, bool value) {
    const DWORD stored = value ? 1 : 0;
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&stored), sizeof(stored));
}

}  // namespace

AppSettings LoadSettings() {
    AppSettings settings;
    Read(L"OpenOnHover", settings.openOnHover);
    Read(L"ShowLyrics", settings.showLyrics);
    Read(L"ShowShuffleAndRepeat", settings.showShuffleAndRepeat);
    Read(L"TintWithArtwork", settings.tintWithArtwork);
    return settings;
}

void SaveSettings(const AppSettings& settings) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    Write(key, L"OpenOnHover", settings.openOnHover);
    Write(key, L"ShowLyrics", settings.showLyrics);
    Write(key, L"ShowShuffleAndRepeat", settings.showShuffleAndRepeat);
    Write(key, L"TintWithArtwork", settings.tintWithArtwork);
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
