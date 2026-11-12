#pragma once

// The user's preferences, one part per feature. The defaults are the app's behavior with nothing
// stored. No Windows headers, so tests run on any host. Matches AppSettings.swift, except for the
// display choice and haptics, which Windows has no use for: there is no notch to prefer and no
// trackpad haptics API for desktop apps.

#include <vector>

#include "features/home/home_settings.h"
#include "features/tasks/tasks_settings.h"
#include "features/usage/usage_settings.h"
#include "island/island_page.h"

namespace noitch {

// The pin shortcut as the system's hotkey control stores it: the virtual key in the low byte and the
// modifiers in the high byte (1 Shift, 2 Ctrl, 4 Alt). 0 is none.
inline constexpr unsigned kHotkeyShift = 1;
inline constexpr unsigned kHotkeyControl = 2;
inline constexpr unsigned kHotkeyAlt = 4;
// Ctrl+Alt+N.
inline constexpr unsigned kDefaultPinHotkey = 'N' | ((kHotkeyControl | kHotkeyAlt) << 8);

// A global shortcut needs Ctrl or Alt: with Shift alone it would steal typing.
inline bool PinHotkeyUsable(unsigned hotkey) { return (hotkey & 0xFF) != 0 && ((hotkey >> 8) & (kHotkeyControl | kHotkeyAlt)) != 0; }

// RegisterHotKey's MOD_ bits (1 Alt, 2 Ctrl, 4 Shift) for a stored shortcut.
inline unsigned PinHotkeyModifiers(unsigned hotkey) {
    const unsigned flags = (hotkey >> 8) & 0xFF;
    return ((flags & kHotkeyAlt) ? 1u : 0u) | ((flags & kHotkeyControl) ? 2u : 0u) | ((flags & kHotkeyShift) ? 4u : 0u);
}

// How the island opens.
struct GeneralSettings {
    bool openOnHover = true;
    // Seconds the pointer must rest in the notch before the island opens. 0 opens at once.
    double hoverDelay = 0;
    // Pins the island open, and lets it go, from anywhere. 0 when turned off.
    unsigned pinHotkey = kDefaultPinHotkey;

    bool operator==(const GeneralSettings&) const = default;
};

inline constexpr double kHoverDelays[] = {0, 0.15, 0.3, 0.5};

enum class OpenPage { Last, Home, Usage, Tasks };

// Which pages the island has, and which it opens on. Home is always there: the island needs one
// page, and it is the one with the controls.
struct PageSettings {
    bool showUsage = true;
    bool showTasks = true;
    OpenPage openOn = OpenPage::Last;

    VisiblePages Visible() const { return {showUsage, showTasks}; }
    bool Shows(IslandPage page) const { return Visible().Shows(page); }

    // The page to show on opening, given the one shown last.
    IslandPage Opening(IslandPage last) const {
        const IslandPage wanted = openOn == OpenPage::Home    ? IslandPage::Home
                                  : openOn == OpenPage::Usage ? IslandPage::Usage
                                  : openOn == OpenPage::Tasks ? IslandPage::Tasks
                                                              : last;
        return Shows(wanted) ? wanted : IslandPage::Home;
    }

    bool operator==(const PageSettings&) const = default;
};

struct AppSettings {
    GeneralSettings general;
    PageSettings pages;
    HomeSettings home;
    UsageSettings usage;
    TasksSettings tasks;

    bool operator==(const AppSettings&) const = default;
};

}  // namespace noitch
