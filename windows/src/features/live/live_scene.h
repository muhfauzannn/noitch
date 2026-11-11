#pragma once

#include <dcomp.h>
#include <wrl/client.h>

#include <memory>

#include "features/now_playing/media_session_source.h"
#include "island/compositor.h"
#include "live_layout.h"

namespace noitch {

// The closed island's music visuals: artwork in the left wing, six thin level bars in the right. It decides
// nothing. The bars are repeating DirectComposition animations, so while they move DWM does every
// frame and this process does nothing; they are a rhythm, not a reading of the audio.
class LiveScene {
public:
    // `left` is the widened pill's left edge in window pixels.
    HRESULT Create(Compositor& compositor, const LiveLayout& layout, float left, float scale);
    void Release();
    bool Exists() const { return root_ != nullptr; }

    // `barColor` is opaque BGRA. Commits.
    HRESULT SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t barColor);
    // Playing, each bar rises and falls on its own period, forever; paused, they rest low. Commits.
    HRESULT SetPlaying(bool playing);
    // Fades in after `delay` seconds, or out at once. `now` is the island clock. Commits.
    HRESULT Show(bool visible, double delay, double now);

private:
    HRESULT PaintBars(uint32_t color);

    Compositor* compositor_ = nullptr;
    LiveLayout layout_{0, 0};
    float scale_ = 1;
    bool playing_ = false;
    uint32_t barColor_ = 0;
    ContentFade opacity_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> effect_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> art_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> artSurface_;
    // One per bar: each has its own shade.
    Microsoft::WRL::ComPtr<IDCompositionSurface> barSurfaces_[LiveLayout::kBarCount];
    Microsoft::WRL::ComPtr<IDCompositionVisual> bars_[LiveLayout::kBarCount];
    Microsoft::WRL::ComPtr<IDCompositionScaleTransform> barScales_[LiveLayout::kBarCount];
};

}  // namespace noitch
