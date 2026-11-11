#include "live_scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace noitch {

using Microsoft::WRL::ComPtr;

namespace {

int Px(float logical, float scale) { return std::max(static_cast<int>(logical * scale + 0.5f), 1); }

// The artwork's empty state: a faint square, like the macOS placeholder's plate.
constexpr uint32_t kPlaceholder = 0x1F1F1F1F;
// The still line a paused wave settles into.
constexpr float kFlatOpacity = 0.5f;

}  // namespace

HRESULT LiveScene::Create(Compositor& compositor, const LiveLayout& layout, float left, float scale) {
    Release();
    compositor_ = &compositor;
    layout_ = layout;
    scale_ = scale;
    IDCompositionDevice* device = compositor.Device();
    if (!device) return E_UNEXPECTED;
    HRESULT hr;
    if (FAILED(hr = device->CreateVisual(&root_))) return hr;
    if (FAILED(hr = device->CreateEffectGroup(&effect_))) return hr;
    if (FAILED(hr = effect_->SetOpacity(0.0f))) return hr;
    opacity_ = {0, 0, 0, 0, 0};
    if (FAILED(hr = root_->SetEffect(effect_.Get()))) return hr;
    if (FAILED(hr = root_->SetOffsetX(left))) return hr;
    if (FAILED(hr = root_->SetOffsetY(0.0f))) return hr;

    const HomeRect art = layout.Artwork();
    if (FAILED(hr = device->CreateVisual(&art_))) return hr;
    if (FAILED(hr = art_->SetOffsetX(static_cast<float>(Px(art.x, scale))))) return hr;
    if (FAILED(hr = art_->SetOffsetY(static_cast<float>(Px(art.y, scale))))) return hr;
    if (FAILED(hr = art_->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR))) return hr;
    if (FAILED(hr = root_->AddVisual(art_.Get(), FALSE, nullptr))) return hr;

    const HomeRect box = layout.WaveBox();
    const int boxWidth = Px(box.width, scale);
    const int boxHeight = Px(box.height, scale);
    if (FAILED(hr = device->CreateVisual(&waveBox_))) return hr;
    if (FAILED(hr = device->CreateRectangleClip(&waveClip_))) return hr;
    if (FAILED(hr = waveClip_->SetLeft(0.0f)) || FAILED(hr = waveClip_->SetTop(0.0f)) ||
        FAILED(hr = waveClip_->SetRight(static_cast<float>(boxWidth))) || FAILED(hr = waveClip_->SetBottom(static_cast<float>(boxHeight)))) return hr;
    if (FAILED(hr = waveBox_->SetClip(waveClip_.Get()))) return hr;
    if (FAILED(hr = waveBox_->SetOffsetX(static_cast<float>(Px(box.x, scale))))) return hr;
    if (FAILED(hr = waveBox_->SetOffsetY(static_cast<float>(Px(box.y, scale))))) return hr;
    if (FAILED(hr = root_->AddVisual(waveBox_.Get(), FALSE, nullptr))) return hr;
    for (int i = 0; i < LiveLayout::kWaveCount; ++i) {
        const LiveLayout::Wave& wave = LiveLayout::kWaves[i];
        int width = 0, height = 0;
        WavePixels(layout, wave, scale, 0xFFFFFFFF, &width, &height);
        if (FAILED(hr = device->CreateSurface(static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &lineSurfaces_[i]))) return hr;
        if (FAILED(hr = device->CreateVisual(&lines_[i]))) return hr;
        if (FAILED(hr = device->CreateEffectGroup(&lineEffects_[i]))) return hr;
        if (FAILED(hr = lineEffects_[i]->SetOpacity(0.0f))) return hr;
        lineOpacity_[i] = {};
        if (FAILED(hr = lines_[i]->SetEffect(lineEffects_[i].Get()))) return hr;
        if (FAILED(hr = lines_[i]->SetContent(lineSurfaces_[i].Get()))) return hr;
        // One length to the left of the box, so a slide either way stays covered.
        if (FAILED(hr = lines_[i]->SetOffsetX(-wave.length * scale))) return hr;
        if (FAILED(hr = waveBox_->AddVisual(lines_[i].Get(), TRUE, nullptr))) return hr;
    }
    const int lineHeight = Px(LiveLayout::kLineWidth, scale);
    if (FAILED(hr = device->CreateSurface(static_cast<UINT>(boxWidth), static_cast<UINT>(lineHeight), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &flatSurface_))) return hr;
    if (FAILED(hr = device->CreateVisual(&flat_))) return hr;
    if (FAILED(hr = device->CreateEffectGroup(&flatEffect_))) return hr;
    flatOpacity_ = {0, 0, 0, kFlatOpacity, kFlatOpacity};
    if (FAILED(hr = flatEffect_->SetOpacity(kFlatOpacity))) return hr;
    if (FAILED(hr = flat_->SetEffect(flatEffect_.Get()))) return hr;
    if (FAILED(hr = flat_->SetContent(flatSurface_.Get()))) return hr;
    if (FAILED(hr = flat_->SetOffsetY(static_cast<float>((boxHeight - lineHeight) / 2)))) return hr;
    if (FAILED(hr = waveBox_->AddVisual(flat_.Get(), TRUE, nullptr))) return hr;
    // Black fading to clear over each end: the pill is black, so the lines dissolve into it.
    const int fade = Px(LiveLayout::kEdgeFade, scale);
    for (int i = 0; i < 2; ++i) {
        const std::vector<uint32_t> pixels = EdgeFadePixels(fade, boxHeight, i == 0);
        if (FAILED(hr = device->CreateSurface(static_cast<UINT>(fade), static_cast<UINT>(boxHeight), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &edgeSurfaces_[i]))) return hr;
        if (FAILED(hr = compositor.Upload(edgeSurfaces_[i].Get(), static_cast<UINT>(fade), static_cast<UINT>(boxHeight), pixels.data()))) return hr;
        if (FAILED(hr = device->CreateVisual(&edges_[i]))) return hr;
        if (FAILED(hr = edges_[i]->SetContent(edgeSurfaces_[i].Get()))) return hr;
        if (FAILED(hr = edges_[i]->SetOffsetX(i == 0 ? 0.0f : static_cast<float>(boxWidth - fade)))) return hr;
        if (FAILED(hr = waveBox_->AddVisual(edges_[i].Get(), TRUE, nullptr))) return hr;
    }
    playing_ = false;
    waveColor_ = 0;
    if (FAILED(hr = SetArtwork(nullptr, 0xFFFFFFFF))) return hr;
    return compositor.AttachLive(root_.Get());
}

void LiveScene::Release() {
    if (compositor_ && root_) compositor_->DetachLive(root_.Get());
    for (auto& visual : lines_) visual.Reset();
    for (auto& effect : lineEffects_) effect.Reset();
    for (auto& surface : lineSurfaces_) surface.Reset();
    for (auto& visual : edges_) visual.Reset();
    for (auto& surface : edgeSurfaces_) surface.Reset();
    flat_.Reset();
    flatEffect_.Reset();
    flatSurface_.Reset();
    waveClip_.Reset();
    waveBox_.Reset();
    artSurface_.Reset();
    art_.Reset();
    effect_.Reset();
    root_.Reset();
    if (compositor_) compositor_->Commit();
}

// The lines and the flat one take the artwork's colour; redrawn only when it changes.
HRESULT LiveScene::PaintWave(uint32_t color) {
    if (color == waveColor_) return S_OK;
    waveColor_ = color;
    HRESULT hr;
    for (int i = 0; i < LiveLayout::kWaveCount; ++i) {
        int width = 0, height = 0;
        const std::vector<uint32_t> pixels = WavePixels(layout_, LiveLayout::kWaves[i], scale_, color | 0xFF000000, &width, &height);
        if (FAILED(hr = compositor_->Upload(lineSurfaces_[i].Get(), static_cast<UINT>(width), static_cast<UINT>(height), pixels.data()))) return hr;
    }
    const int width = Px(layout_.WaveBox().width, scale_);
    const int height = Px(LiveLayout::kLineWidth, scale_);
    std::vector<uint32_t> line(static_cast<size_t>(width) * height, color | 0xFF000000);
    return compositor_->Upload(flatSurface_.Get(), static_cast<UINT>(width), static_cast<UINT>(height), line.data());
}

HRESULT LiveScene::SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t waveColor) {
    if (!root_) return S_OK;
    IDCompositionDevice* device = compositor_->Device();
    const int side = Px(layout_.ArtSide(), scale_);
    HRESULT hr;
    if (artwork && artwork->size > 0) {
        // Rounded here, once, instead of clipping every frame.
        const std::vector<uint32_t> rounded = RoundedCorners(artwork->pixels.data(), artwork->size, 5.0f * artwork->size / static_cast<float>(side));
        ComPtr<IDCompositionSurface> surface;
        const UINT size = static_cast<UINT>(artwork->size);
        if (FAILED(hr = device->CreateSurface(size, size, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return hr;
        if (FAILED(hr = compositor_->Upload(surface.Get(), size, size, rounded.data()))) return hr;
        D2D_MATRIX_3X2_F fit{};
        fit._11 = fit._22 = static_cast<float>(side) / static_cast<float>(artwork->size);
        if (FAILED(hr = art_->SetTransform(fit))) return hr;
        artSurface_ = surface;
    } else {
        const std::vector<uint32_t> plate = RoundedCorners(std::vector<uint32_t>(static_cast<size_t>(side) * side, kPlaceholder).data(), side, 5.0f * scale_);
        ComPtr<IDCompositionSurface> surface;
        if (FAILED(hr = device->CreateSurface(static_cast<UINT>(side), static_cast<UINT>(side), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return hr;
        if (FAILED(hr = compositor_->Upload(surface.Get(), static_cast<UINT>(side), static_cast<UINT>(side), plate.data()))) return hr;
        D2D_MATRIX_3X2_F identity{};
        identity._11 = identity._22 = 1;
        if (FAILED(hr = art_->SetTransform(identity))) return hr;
        artSurface_ = surface;
    }
    if (FAILED(hr = art_->SetContent(artSurface_.Get()))) return hr;
    if (FAILED(hr = PaintWave(waveColor))) return hr;
    return compositor_->Commit();
}

HRESULT LiveScene::Fade(IDCompositionEffectGroup* effect, ContentFade& fade, float to, double now) {
    fade = {now, 0, 0.35, static_cast<float>(fade.ValueAt(now)), to};
    ComPtr<IDCompositionAnimation> animation;
    HRESULT hr = compositor_->BuildFade(fade, &animation);
    return FAILED(hr) ? hr : effect->SetOpacity(animation.Get());
}

HRESULT LiveScene::SetPlaying(bool playing, double now) {
    if (!root_ || playing == playing_) return S_OK;
    playing_ = playing;
    IDCompositionDevice* device = compositor_->Device();
    HRESULT hr;
    for (int i = 0; i < LiveLayout::kWaveCount; ++i) {
        const LiveLayout::Wave& wave = LiveLayout::kWaves[i];
        const float rest = -wave.length * scale_;
        const float travel = wave.direction * wave.length * scale_;
        if (playing) {
            // A straight line at constant speed, repeated: one length per period, forever.
            ComPtr<IDCompositionAnimation> drift;
            if (FAILED(hr = device->CreateAnimation(&drift))) return hr;
            if (FAILED(hr = drift->AddCubic(0, rest, static_cast<float>(travel / wave.period), 0, 0))) return hr;
            if (FAILED(hr = drift->AddRepeat(wave.period, wave.period))) return hr;
            if (FAILED(hr = lines_[i]->SetOffsetX(drift.Get()))) return hr;
            driftStart_[i] = now;
        } else {
            // Stopped where it is, so the fade doesn't jump, and nothing runs while paused.
            const double phase = std::fmod(now - driftStart_[i], wave.period) / wave.period;
            if (FAILED(hr = lines_[i]->SetOffsetX(rest + travel * static_cast<float>(phase)))) return hr;
        }
        if (FAILED(hr = Fade(lineEffects_[i].Get(), lineOpacity_[i], playing ? wave.opacity : 0.0f, now))) return hr;
    }
    if (FAILED(hr = Fade(flatEffect_.Get(), flatOpacity_, playing ? 0.0f : kFlatOpacity, now))) return hr;
    return compositor_->Commit();
}

HRESULT LiveScene::Show(bool visible, double delay, double now) {
    if (!root_) return S_OK;
    const float from = static_cast<float>(opacity_.ValueAt(now));
    const float to = visible ? 1.0f : 0.0f;
    if (from == to && opacity_.to == to) return S_OK;
    opacity_ = {now, visible ? delay : 0, visible ? 0.3 : 0.12, from, to};
    ComPtr<IDCompositionAnimation> fade;
    HRESULT hr;
    if (FAILED(hr = compositor_->BuildFade(opacity_, &fade))) return hr;
    if (FAILED(hr = effect_->SetOpacity(fade.Get()))) return hr;
    return compositor_->Commit();
}

}  // namespace noitch
