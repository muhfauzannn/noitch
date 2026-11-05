#pragma once

#include <windows.h>
#include <windows.media.control.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "now_playing.h"

namespace noitch {

// A decoded, square cover in premultiplied BGRA, sized for display.
struct Artwork {
    int size = 0;
    std::vector<uint32_t> pixels;
};

// Reads now playing from Windows' media sessions and sends them commands. Lazily started: WinRT isn't
// touched until the island first opens. WinRT calls back on pool threads, which only post a message,
// so all state stays on the main thread and no thread of ours exists.
class MediaSessionSource {
public:
    static constexpr UINT kMessage = WM_APP + 1;

    void Start(HWND notify, double (*now)());
    void Stop();

    // Handles kMessage. Returns true if `state` or `artwork` changed.
    bool HandleMessage(WPARAM kind, LPARAM payload);

    const NowPlaying& State() const { return state_; }
    std::shared_ptr<const Artwork> CurrentArtwork() const { return artwork_; }

    void OpenApp();
    void TogglePlayPause();
    void Next();
    void Previous();
    void Seek(double seconds);
    void SetShuffle(bool on);
    void SetRepeat(RepeatMode mode);

    void SetArtworkPixels(int pixels) { artworkPixels_ = pixels; }

private:
    using Manager = ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManager;
    using Session = ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSession;

    void BindSession();
    void UnbindSession();
    void ReadTimelineAndPlayback();
    void RequestMediaProperties();

    HWND notify_ = nullptr;
    double (*now_)() = nullptr;
    bool running_ = false;
    bool roInitialized_ = false;
    int artworkPixels_ = 0;
    // Results of a superseded request arrive late and are dropped by this.
    uint32_t generation_ = 0;
    Microsoft::WRL::ComPtr<Manager> manager_;
    Microsoft::WRL::ComPtr<Session> session_;
    EventRegistrationToken sessionChangedToken_{};
    EventRegistrationToken mediaToken_{};
    EventRegistrationToken timelineToken_{};
    EventRegistrationToken playbackToken_{};
    NowPlaying state_;
    std::shared_ptr<const Artwork> artwork_;
};

}  // namespace noitch
