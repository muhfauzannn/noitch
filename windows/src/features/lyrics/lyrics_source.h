#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "lyrics.h"

namespace noitch {

// Finds lyrics on LRCLIB and keeps them on disk, so a track is only ever looked up once. The blocking
// work runs on a short-lived pool thread, which ends by posting the result to the main thread.
class LyricsSource {
public:
    static constexpr UINT kMessage = WM_APP + 2;

    // wparam of kMessage is `generation`, and lparam a Lyrics* that the receiver must delete.
    static void LookUp(HWND notify, uintptr_t generation, const std::string& title, const std::string& artist,
                       const std::string& album, double duration);
};

}  // namespace noitch
