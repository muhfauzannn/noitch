#pragma once

// The pages of the settings window that belong to the app rather than one feature. Matches
// SettingsPanes.swift, without the display and haptics rows (see app_settings.h).

#include "settings_pane.h"

namespace noitch {

// General's opening and display rows; GeneralSettingsPane adds the pin.
inline SettingsPane GeneralSettingsPaneBase() {
    std::vector<std::pair<std::wstring, double>> delays;
    for (const double delay : kHoverDelays) delays.emplace_back(delay == 0 ? L"None" : SecondsLabel(delay), delay);
    SettingsRow launch;
    launch.kind = SettingsRow::Kind::LaunchAtLogin;
    launch.title = L"Launch at login";
    return {L"General",
            {
                {L"Startup", L"", {launch}},
                {L"Opening",
                 L"A click on the notch always opens the island.",
                 {
                     ToggleRow(L"Open on hover", L"When off, click the notch to open it.",
                               [](const AppSettings& s) { return s.general.openOnHover; },
                               [](AppSettings& s, bool on) { s.general.openOnHover = on; }),
                     ChoiceRow(L"Hover delay", L"How long the pointer rests in the notch first.", delays,
                               [](const AppSettings& s) { return s.general.hoverDelay; },
                               [](AppSettings& s, double value) { s.general.hoverDelay = value; },
                               [](const AppSettings& s) { return s.general.openOnHover; }),
                 }},
            },
            {}};
}

// The shortcut that pins the island, as its own group at the end of General.
inline SettingsPane WithPinSection(SettingsPane pane) {
    SettingsRow hotkey;
    hotkey.kind = SettingsRow::Kind::Hotkey;
    hotkey.title = L"Pin shortcut";
    hotkey.caption = L"Click, then press the keys. Backspace turns it off.";
    hotkey.value = [](const AppSettings& s) { return static_cast<double>(s.general.pinHotkey); };
    hotkey.setValue = [](AppSettings& s, double value) { s.general.pinHotkey = static_cast<unsigned>(value); };
    pane.sections.push_back({L"Pin", L"Pinned, the island stays open when the pointer leaves. Pin it from the header, or from anywhere with the shortcut.", {hotkey}});
    return pane;
}

inline SettingsPane GeneralSettingsPane() { return WithPinSection(GeneralSettingsPaneBase()); }

inline SettingsPane PagesSettingsPane() {
    return {L"Pages",
            {
                {L"Pages",
                 L"Home is always there. Swipe sideways in the island to move between pages.",
                 {ToggleRow(L"AI Usage", L"Claude Code and Codex limits.", [](const AppSettings& s) { return s.pages.showUsage; },
                            [](AppSettings& s, bool on) { s.pages.showUsage = on; }),
                  ToggleRow(L"Shelf", L"Files and clipboard history.", [](const AppSettings& s) { return s.pages.showShelf; },
                            [](AppSettings& s, bool on) { s.pages.showShelf = on; }),
                  ToggleRow(L"Tasks", L"What's still due on your Moodle site, such as Scele.", [](const AppSettings& s) { return s.pages.showTasks; },
                            [](AppSettings& s, bool on) { s.pages.showTasks = on; })}},
                {L"Opening",
                 L"",
                 {ChoiceRow(L"Open on", L"", {{L"The page used last", 0}, {L"Home", 1}, {L"AI Usage", 2}, {L"Tasks", 3}, {L"Shelf", 4}},
                            [](const AppSettings& s) { return static_cast<double>(s.pages.openOn); },
                            [](AppSettings& s, double value) { s.pages.openOn = static_cast<OpenPage>(static_cast<int>(value)); })}},
            },
            {}};
}

inline SettingsPane AboutSettingsPane() {
    SettingsRow version;
    version.kind = SettingsRow::Kind::Text;
    version.title = L"Noitch for Windows";
    return {L"About", {{L"", L"", {version}}}, L"Lyrics come from LRCLIB."};
}

}  // namespace noitch
