#include "live_scene.h"

#include <algorithm>
#include <vector>

namespace noitch {

using Microsoft::WRL::ComPtr;

namespace {

int Px(float logical, float scale) { return std::max(static_cast<int>(logical * scale + 0.5f), 1); }

// The artwork's empty state: a faint square, like the macOS placeholder's plate.
constexpr uint32_t kPlaceholder = 0x1F1F1F1F;

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

    const HomeRect first = layout.Bar(0);
    const UINT barWidth = static_cast<UINT>(Px(first.width, scale));
    const UINT barHeight = static_cast<UINT>(Px(first.height, scale));
    for (int i = 0; i < LiveLayout::kBarCount; ++i) {
        const HomeRect bar = layout.Bar(i);
        if (FAILED(hr = device->CreateSurface(barWidth, barHeight, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &barSurfaces_[i]))) return hr;
        if (FAILED(hr = device->CreateVisual(&bars_[i]))) return hr;
        if (FAILED(hr = device->CreateScaleTransform(&barScales_[i]))) return hr;
        if (FAILED(hr = barScales_[i]->SetCenterY(barHeight / 2.0f))) return hr;
        if (FAILED(hr = barScales_[i]->SetScaleY(LiveLayout::kRestingScale))) return hr;
        if (FAILED(hr = bars_[i]->SetTransform(barScales_[i].Get()))) return hr;
        if (FAILED(hr = bars_[i]->SetContent(barSurfaces_[i].Get()))) return hr;
        if (FAILED(hr = bars_[i]->SetOffsetX(static_cast<float>(Px(bar.x, scale))))) return hr;
        if (FAILED(hr = bars_[i]->SetOffsetY(static_cast<float>(Px(bar.y, scale))))) return hr;
        if (FAILED(hr = root_->AddVisual(bars_[i].Get(), FALSE, nullptr))) return hr;
    }
    playing_ = false;
    barColor_ = 0;
    if (FAILED(hr = SetArtwork(nullptr, 0xFFFFFFFF))) return hr;
    return compositor.AttachLive(root_.Get());
}

void LiveScene::Release() {
    if (compositor_ && root_) compositor_->DetachLive(root_.Get());
    for (auto& bar : bars_) bar.Reset();
    for (auto& transform : barScales_) transform.Reset();
    for (auto& surface : barSurfaces_) surface.Reset();
    artSurface_.Reset();
    art_.Reset();
    effect_.Reset();
    root_.Reset();
    if (compositor_) compositor_->Commit();
}

HRESULT LiveScene::PaintBars(uint32_t color) {
    if (color == barColor_) return S_OK;
    barColor_ = color;
    const HomeRect bar = layout_.Bar(0);
    const int width = Px(bar.width, scale_);
    const int height = Px(bar.height, scale_);
    for (int i = 0; i < LiveLayout::kBarCount; ++i) {
        const std::vector<uint32_t> pixels = BarPixels(width, height, TowardWhite(color | 0xFF000000, LiveLayout::kBarWhiteness[i]));
        const HRESULT hr = compositor_->Upload(barSurfaces_[i].Get(), static_cast<UINT>(width), static_cast<UINT>(height), pixels.data());
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

HRESULT LiveScene::SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t barColor) {
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
    if (FAILED(hr = PaintBars(barColor))) return hr;
    return compositor_->Commit();
}

HRESULT LiveScene::SetPlaying(bool playing) {
    if (!root_ || playing == playing_) return S_OK;
    playing_ = playing;
    IDCompositionDevice* device = compositor_->Device();
    HRESULT hr;
    for (int i = 0; i < LiveLayout::kBarCount; ++i) {
        if (!playing) {
            if (FAILED(hr = barScales_[i]->SetScaleY(LiveLayout::kRestingScale))) return hr;
            continue;
        }
        const double period = LiveLayout::kBarPeriods[i];
        // Every other bar starts at the top, so they don't rise together.
        const bool high = i % 2 == 1;
        const auto cycle = BarCycle(period, high ? 1.0f : LiveLayout::kRestingScale, high ? LiveLayout::kRestingScale : 1.0f);
        ComPtr<IDCompositionAnimation> animation;
        if (FAILED(hr = device->CreateAnimation(&animation))) return hr;
        for (const BarSegment& s : cycle) {
            if (FAILED(hr = animation->AddCubic(s.offset, s.c0, s.c1, s.c2, s.c3))) return hr;
        }
        if (FAILED(hr = animation->AddRepeat(period * 2, period * 2))) return hr;
        if (FAILED(hr = barScales_[i]->SetScaleY(animation.Get()))) return hr;
    }
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
