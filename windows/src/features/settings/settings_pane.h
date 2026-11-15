#pragma once

// One page of the settings window, described as data: the window turns it into controls. Each
// feature describes its own page next to its code. No Windows headers, so tests run on any host.

#include <cmath>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#include "app_settings.h"

namespace noitch {

struct SettingsRow {
    // Account is the Moodle sign-in block, which the window builds itself (see TasksSettingsPane).
    // Hotkey is a shortcut field, the system's hotkey control; `value` and `setValue` carry it.
    enum class Kind { Toggle, Choice, Slider, LaunchAtLogin, Text, Account, Hotkey };

    int minimum = 0;
    int maximum = 0;
    Kind kind = Kind::Toggle;
    std::wstring title;
    std::wstring caption;
    bool (*isOn)(const AppSettings&) = nullptr;
    void (*setOn)(AppSettings&, bool) = nullptr;
    // For a choice: labelled values, and how to read and write the one picked.
    std::vector<std::pair<std::wstring, double>> options;
    double (*value)(const AppSettings&) = nullptr;
    void (*setValue)(AppSettings&, double) = nullptr;
    // When set, the row only means something while this is true.
    bool (*enabled)(const AppSettings&) = nullptr;
};

struct SettingsSection {
    std::wstring title;
    std::wstring footer;
    std::vector<SettingsRow> rows;
};

struct SettingsPane {
    std::wstring title;
    std::vector<SettingsSection> sections;
    // Text under the groups, such as credits.
    std::wstring note;
};

inline SettingsRow ToggleRow(std::wstring title, std::wstring caption, bool (*isOn)(const AppSettings&), void (*setOn)(AppSettings&, bool),
                             bool (*enabled)(const AppSettings&) = nullptr) {
    SettingsRow row;
    row.kind = SettingsRow::Kind::Toggle;
    row.title = std::move(title);
    row.caption = std::move(caption);
    row.isOn = isOn;
    row.setOn = setOn;
    row.enabled = enabled;
    return row;
}

inline SettingsRow ChoiceRow(std::wstring title, std::wstring caption, std::vector<std::pair<std::wstring, double>> options,
                             double (*value)(const AppSettings&), void (*setValue)(AppSettings&, double),
                             bool (*enabled)(const AppSettings&) = nullptr) {
    SettingsRow row;
    row.kind = SettingsRow::Kind::Choice;
    row.title = std::move(title);
    row.caption = std::move(caption);
    row.options = std::move(options);
    row.value = value;
    row.setValue = setValue;
    row.enabled = enabled;
    return row;
}

inline SettingsRow SliderRow(std::wstring title, int minimum, int maximum, double (*value)(const AppSettings&), void (*setValue)(AppSettings&, double)) {
    SettingsRow row;
    row.kind = SettingsRow::Kind::Slider;
    row.title = std::move(title);
    row.minimum = minimum;
    row.maximum = maximum;
    row.value = value;
    row.setValue = setValue;
    return row;
}

// The index of the option holding `value`, or -1.
inline int ChoiceIndex(const SettingsRow& row, double value) {
    for (size_t i = 0; i < row.options.size(); ++i) {
        if (std::fabs(row.options[i].second - value) < 1e-6) return static_cast<int>(i);
    }
    return -1;
}

// "0.15 s", or "+0.5 s" and "−0.5 s" when `isSigned`.
inline std::wstring SecondsLabel(double value, bool isSigned = false) {
    wchar_t text[32];
    std::swprintf(text, 32, L"%g s", std::fabs(value));
    if (!isSigned || value == 0) return text;
    return std::wstring(value > 0 ? L"+" : L"\x2212") + text;
}

}  // namespace noitch
