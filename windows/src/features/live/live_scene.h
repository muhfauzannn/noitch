#pragma once

#include <dcomp.h>
#include <wrl/client.h>

#include <memory>

#include "features/now_playing/media_session_source.h"
#include "island/compositor.h"
#include "live_layout.h"

namespace noitch {

// The closed island's music visuals: artwork in the left wing, a flowing wave in the right. It
// decides nothing. Each wave line is drawn once and slid sideways by a repeating DirectComposition
// animation, so while it moves DWM does every frame and this process does nothing. It shows that
// something plays; it is not a reading of the audio.
class LiveScene {
public:
    // `left` is the widened pill's left edge in window pixels.
    HRESULT Create(Compositor& compositor, const LiveLayout& layout, float left, float scale);
    void Release();
    bool Exists() const { return root_ != nullptr; }

    // `waveColor` is opaque BGRA. Commits.
    HRESULT SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t waveColor);
    // Playing, the lines drift forever; paused, they fade into one still line. `now` is the island
    // clock. Commits.
    HRESULT SetPlaying(bool playing, double now);
    // Fades in after `delay` seconds, or out at once. `now` is the island clock. Commits.
    HRESULT Show(bool visible, double delay, double now);

private:
    HRESULT PaintWave(uint32_t color);
    HRESULT Fade(IDCompositionEffectGroup* effect, ContentFade& fade, float to, double now);

    Compositor* compositor_ = nullptr;
    LiveLayout layout_{0, 0};
    float scale_ = 1;
    bool playing_ = false;
    uint32_t waveColor_ = 0;
    ContentFade opacity_;
    // When each line's drift began, so a pause can stop it where it is.
    double driftStart_[LiveLayout::kWaveCount] = {};
    ContentFade lineOpacity_[LiveLayout::kWaveCount];
    ContentFade flatOpacity_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> effect_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> art_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> artSurface_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> waveBox_;
    Microsoft::WRL::ComPtr<IDCompositionRectangleClip> waveClip_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> lines_[LiveLayout::kWaveCount];
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> lineEffects_[LiveLayout::kWaveCount];
    Microsoft::WRL::ComPtr<IDCompositionSurface> lineSurfaces_[LiveLayout::kWaveCount];
    Microsoft::WRL::ComPtr<IDCompositionVisual> flat_;
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> flatEffect_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> flatSurface_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> edges_[2];
    Microsoft::WRL::ComPtr<IDCompositionSurface> edgeSurfaces_[2];
};

}  // namespace noitch
