#pragma once

#include <windows.h>

#include <cstdint>

#include "ai_limits.h"

namespace noitch {

// Asks Anthropic for Claude Code's five-hour and weekly limits, the numbers its /usage command shows.
// This uses Claude Code's own login (.credentials.json) and an endpoint Anthropic doesn't document:
// an approved exception, limited as described in docs/architecture.md.
//
// One request when the usage page is shown and one a minute while it stays shown; none otherwise.
// The token is read for each request and never stored, logged or refreshed. Refreshing would replace
// Claude Code's own login and sign it out, so an expired token is only reported. The blocking work
// runs on a short-lived pool thread, which ends by posting the result to the main thread.
class ClaudeLimitSource {
public:
    static constexpr UINT kMessage = WM_APP + 3;

    // wparam of kMessage is `generation`, and lparam a ToolLimits* that the receiver must delete.
    static void Fetch(HWND notify, uintptr_t generation, double now);
};

}  // namespace noitch
