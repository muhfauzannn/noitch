#pragma once

#include "app_settings.h"

namespace noitch {

// Settings live in HKCU\Software\Noitch, and launch at login is the usual Run value.
AppSettings LoadSettings();
void SaveSettings(const AppSettings& settings);
bool LaunchesAtLogin();
bool SetLaunchesAtLogin(bool enabled);

}  // namespace noitch
