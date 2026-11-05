#pragma once

namespace noitch {

// The user's preferences. The defaults are the app's behavior without any settings stored.
struct AppSettings {
    bool openOnHover = true;
    bool showLyrics = true;
    bool showShuffleAndRepeat = true;
    bool tintWithArtwork = true;

    bool operator==(const AppSettings&) const = default;
};

}  // namespace noitch
