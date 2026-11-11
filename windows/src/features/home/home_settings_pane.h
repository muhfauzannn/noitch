#pragma once

// Home's page in the settings window. Matches HomeSettingsPane.swift.

#include "features/settings/settings_pane.h"

namespace noitch {

inline SettingsPane HomeSettingsPane() {
    std::vector<std::pair<std::wstring, double>> offsets;
    for (const double offset : kLyricsOffsets) offsets.emplace_back(offset == 0 ? L"As published" : SecondsLabel(offset, true), offset);
    const auto lyricsOn = [](const AppSettings& s) { return s.home.showLyrics; };
    return {L"Home",
            {
                {L"Closed island",
                 L"",
                 {
                     ToggleRow(L"Show what's playing", L"Artwork and level bars beside the notch.",
                               [](const AppSettings& s) { return s.home.showLive; }, [](AppSettings& s, bool on) { s.home.showLive = on; }),
                     ToggleRow(L"Show the lyric line", L"The line being sung, small, under the notch. Needs Show lyrics.",
                               [](const AppSettings& s) { return s.home.showLiveLyrics; },
                               [](AppSettings& s, bool on) { s.home.showLiveLyrics = on; },
                               [](const AppSettings& s) { return s.home.showLive; }),
                 }},
                {L"Lyrics",
                 L"Lyrics come from lrclib.net, which receives the track, artist and album names.",
                 {
                     ToggleRow(L"Show lyrics", L"", lyricsOn, [](AppSettings& s, bool on) { s.home.showLyrics = on; }),
                     ToggleRow(L"Show the next line", L"A dimmer line under the one being sung.",
                               [](const AppSettings& s) { return s.home.showNextLyric; },
                               [](AppSettings& s, bool on) { s.home.showNextLyric = on; }, lyricsOn),
                     ChoiceRow(L"Timing", L"Move lyrics earlier or later when they don't match the song.", offsets,
                               [](const AppSettings& s) { return s.home.lyricsOffset; },
                               [](AppSettings& s, double value) { s.home.lyricsOffset = value; }, lyricsOn),
                 }},
                {L"Controls",
                 L"",
                 {ToggleRow(L"Show shuffle and repeat", L"", [](const AppSettings& s) { return s.home.showShuffleAndRepeat; },
                            [](AppSettings& s, bool on) { s.home.showShuffleAndRepeat = on; })}},
                {L"Artwork color",
                 L"",
                 {
                     ToggleRow(L"Tint the progress bar", L"", [](const AppSettings& s) { return s.home.tintWithArtwork; },
                               [](AppSettings& s, bool on) { s.home.tintWithArtwork = on; }),
                     ToggleRow(L"Glow behind the page", L"A soft wash of the artwork's color.",
                               [](const AppSettings& s) { return s.home.artworkGlow; },
                               [](AppSettings& s, bool on) { s.home.artworkGlow = on; }),
                 }},
            },
            {}};
}

}  // namespace noitch
