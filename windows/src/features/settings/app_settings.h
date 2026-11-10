#pragma once

// The user's preferences, one part per feature. The defaults are the app's behavior with nothing
// stored. No Windows headers, so tests run on any host. Matches AppSettings.swift, except for the
// display choice and haptics, which Windows has no use for: there is no notch to prefer and no
// trackpad haptics API for desktop apps.

#include <vector>

#include "features/home/home_settings.h"
#include "features/usage/usage_settings.h"
#include "island/island_page.h"

namespace noitch {

// How the island opens.
struct GeneralSettings {
    bool openOnHover = true;
    // Seconds the pointer must rest in the notch before the island opens. 0 opens at once.
    double hoverDelay = 0;

    bool operator==(const GeneralSettings&) const = default;
};

inline constexpr double kHoverDelays[] = {0, 0.15, 0.3, 0.5};

enum class OpenPage { Last, Home, Usage };

// Which pages the island has, and which it opens on. Home is always there: the island needs one
// page, and it is the one with the controls.
struct PageSettings {
    bool showUsage = true;
    OpenPage openOn = OpenPage::Last;

    bool Shows(IslandPage page) const { return page == IslandPage::Home || showUsage; }

    // The page to show on opening, given the one shown last.
    IslandPage Opening(IslandPage last) const {
        const IslandPage wanted = openOn == OpenPage::Home ? IslandPage::Home : openOn == OpenPage::Usage ? IslandPage::Usage : last;
        return Shows(wanted) ? wanted : IslandPage::Home;
    }

    bool operator==(const PageSettings&) const = default;
};

struct AppSettings {
    GeneralSettings general;
    PageSettings pages;
    HomeSettings home;
    UsageSettings usage;

    bool operator==(const AppSettings&) const = default;
};

}  // namespace noitch
