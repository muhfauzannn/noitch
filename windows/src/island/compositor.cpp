#include "compositor.h"

#include <array>
#include <cstdint>

#include "flare.h"

using Microsoft::WRL::ComPtr;

namespace noitch {

namespace {

// Larger than any window at any DPI. The clip decides what is visible, so the exact size is moot.
constexpr float kFillScale = 4096;

}  // namespace

HRESULT Compositor::Create(HWND hwnd) {
    hwnd_ = hwnd;
    return BuildScene();
}

HRESULT Compositor::BuildScene() {
    target_.Reset();
    root_.Reset();
    body_.Reset();
    fill_.Reset();
    content_.Reset();
    contentEffect_.Reset();
    for (auto& layer : layers_) layer.Reset();
    for (auto& effect : layerEffects_) effect.Reset();
    dxgi_.Reset();
    leftFlare_.Reset();
    rightFlare_.Reset();
    clip_.Reset();
    surface_.Reset();
    device_.Reset();
    context_.Reset();

    ComPtr<ID3D11Device> d3d;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED,
                                   nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, &context_);
    if (FAILED(hr)) return hr;

    if (FAILED(hr = d3d.As(&dxgi_))) return hr;
    if (FAILED(hr = DCompositionCreateDevice(dxgi_.Get(), IID_PPV_ARGS(&device_)))) return hr;
    if (FAILED(hr = device_->CreateTargetForHwnd(hwnd_, TRUE, &target_))) return hr;
    if (FAILED(hr = device_->CreateVisual(&root_))) return hr;
    if (FAILED(hr = device_->CreateVisual(&body_))) return hr;
    if (FAILED(hr = device_->CreateVisual(&fill_))) return hr;
    if (FAILED(hr = device_->CreateVisual(&leftFlare_))) return hr;
    if (FAILED(hr = device_->CreateVisual(&rightFlare_))) return hr;
    if (FAILED(hr = device_->CreateRectangleClip(&clip_))) return hr;
    if (FAILED(hr = device_->CreateSurface(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface_))) return hr;
    const std::uint32_t opaqueBlack = 0xFF000000;
    if (FAILED(hr = Paint(surface_.Get(), 1, 1, &opaqueBlack))) return hr;

    D2D_MATRIX_3X2_F stretch{};
    stretch._11 = kFillScale;
    stretch._22 = kFillScale;
    if (FAILED(hr = fill_->SetContent(surface_.Get()))) return hr;
    if (FAILED(hr = fill_->SetTransform(stretch))) return hr;
    if (FAILED(hr = fill_->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR))) return hr;

    if (FAILED(hr = root_->SetBorderMode(DCOMPOSITION_BORDER_MODE_SOFT))) return hr;
    if (FAILED(hr = body_->SetClip(clip_.Get()))) return hr;
    if (FAILED(hr = body_->AddVisual(fill_.Get(), FALSE, nullptr))) return hr;
    if (FAILED(hr = root_->AddVisual(body_.Get(), FALSE, nullptr))) return hr;
    if (FAILED(hr = root_->AddVisual(leftFlare_.Get(), FALSE, nullptr))) return hr;
    if (FAILED(hr = root_->AddVisual(rightFlare_.Get(), FALSE, nullptr))) return hr;
    if (FAILED(hr = target_->SetRoot(root_.Get()))) return hr;
    return flareRadius_ > 0 ? SetFlareRadius(flareRadius_) : S_OK;
}

HRESULT Compositor::SetFlareRadius(int radius) {
    flareRadius_ = std::min(radius, kMaxFlareRadiusPx);
    HRESULT hr = BuildFlare(leftFlare_.Get(), false);
    return FAILED(hr) ? hr : BuildFlare(rightFlare_.Get(), true);
}

HRESULT Compositor::BuildFlare(IDCompositionVisual* visual, bool mirrored) {
    std::array<std::uint32_t, (kMaxFlareRadiusPx + kFlareOverlapPx) * kMaxFlareRadiusPx> pixels;
    FillFlarePixels(flareRadius_, mirrored, pixels.data());

    const UINT width = static_cast<UINT>(flareRadius_ + kFlareOverlapPx);
    const UINT height = static_cast<UINT>(flareRadius_);
    ComPtr<IDCompositionSurface> surface;
    HRESULT hr = device_->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = Paint(surface.Get(), width, height, pixels.data()))) return hr;
    return visual->SetContent(surface.Get());
}

HRESULT Compositor::Paint(IDCompositionSurface* surface, UINT width, UINT height, const void* pixels) {
    ComPtr<IDXGISurface> dxgiSurface;
    POINT offset{};
    HRESULT hr = surface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurface), &offset);
    if (FAILED(hr)) return hr;

    // The surface can be a region of a shared atlas, so write only the area at `offset`.
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(hr = dxgiSurface.As(&texture))) {
        const UINT x = static_cast<UINT>(offset.x);
        const UINT y = static_cast<UINT>(offset.y);
        const D3D11_BOX box{x, y, 0, x + width, y + height, 1};
        const UINT rowPitch = width * sizeof(std::uint32_t);
        context_->UpdateSubresource(texture.Get(), 0, &box, pixels, rowPitch, rowPitch * height);
    }
    const HRESULT end = surface->EndDraw();
    return FAILED(hr) ? hr : end;
}

HRESULT Compositor::ShowPill(const Pill& pill) {
    HRESULT hr;
    if (FAILED(hr = clip_->SetTop(0.0f))) return hr;
    if (FAILED(hr = clip_->SetLeft(pill.left))) return hr;
    if (FAILED(hr = clip_->SetRight(pill.right))) return hr;
    if (FAILED(hr = clip_->SetBottom(pill.bottom))) return hr;
    if (FAILED(hr = clip_->SetBottomLeftRadiusX(pill.radius))) return hr;
    if (FAILED(hr = clip_->SetBottomLeftRadiusY(pill.radius))) return hr;
    if (FAILED(hr = clip_->SetBottomRightRadiusX(pill.radius))) return hr;
    if (FAILED(hr = clip_->SetBottomRightRadiusY(pill.radius))) return hr;
    if (FAILED(hr = leftFlare_->SetOffsetX(pill.left - flareRadius_))) return hr;
    if (FAILED(hr = rightFlare_->SetOffsetX(pill.right - kFlareOverlapPx))) return hr;
    return device_->Commit();
}

// `bias` shifts the whole curve, so a flare can follow a pill edge at a fixed distance.
HRESULT Compositor::BuildAnimation(const PillMotion& motion, int channel, float bias, IDCompositionAnimation** animation) {
    ComPtr<IDCompositionAnimation> result;
    HRESULT hr = device_->CreateAnimation(&result);
    if (FAILED(hr)) return hr;

    for (int i = 0; i < motion.SegmentCount(); ++i) {
        const CubicSegment s = motion.Segment(channel, i);
        if (FAILED(hr = result->AddCubic(s.offset, s.c0 + bias, s.c1, s.c2, s.c3))) return hr;
    }
    if (FAILED(hr = result->End(motion.Duration(), static_cast<float>(motion.Channel(channel).Target()) + bias))) return hr;
    *animation = result.Detach();
    return S_OK;
}

HRESULT Compositor::AnimatePill(const PillMotion& motion) {
    ComPtr<IDCompositionAnimation> left, right, bottom, radius, leftFlare, rightFlare;
    HRESULT hr;
    if (FAILED(hr = BuildAnimation(motion, 0, 0, &left))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 1, 0, &right))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 2, 0, &bottom))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 3, 0, &radius))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 0, static_cast<float>(-flareRadius_), &leftFlare))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 1, -static_cast<float>(kFlareOverlapPx), &rightFlare))) return hr;

    if (FAILED(hr = clip_->SetLeft(left.Get()))) return hr;
    if (FAILED(hr = clip_->SetRight(right.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottom(bottom.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomLeftRadiusX(radius.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomLeftRadiusY(radius.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomRightRadiusX(radius.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomRightRadiusY(radius.Get()))) return hr;
    if (FAILED(hr = leftFlare_->SetOffsetX(leftFlare.Get()))) return hr;
    if (FAILED(hr = rightFlare_->SetOffsetX(rightFlare.Get()))) return hr;
    return device_->Commit();
}

HRESULT Compositor::RecoverIfLost(const Pill& pill) {
    BOOL valid = FALSE;
    HRESULT hr = device_->CheckDeviceState(&valid);
    if (SUCCEEDED(hr) && valid) return S_OK;
    ++generation_;
    if (FAILED(hr = BuildScene())) return hr;
    return ShowPill(pill);
}

HRESULT Compositor::CreateLayerSurface(int layer, UINT width, UINT height, float left, IDCompositionSurface** surface) {
    if (layer < 0 || layer >= kLayerCount) return E_INVALIDARG;
    HRESULT hr;
    if (!content_) {
        if (FAILED(hr = device_->CreateVisual(&content_))) return hr;
        // Above fill_, so the black pill stays behind it.
        if (FAILED(hr = body_->AddVisual(content_.Get(), TRUE, fill_.Get()))) return hr;
    }
    if (FAILED(hr = content_->SetOffsetX(left))) return hr;
    auto& visual = layers_[layer];
    if (!visual) {
        if (FAILED(hr = device_->CreateVisual(&visual))) return hr;
        if (FAILED(hr = device_->CreateEffectGroup(&layerEffects_[layer]))) return hr;
        if (FAILED(hr = visual->SetEffect(layerEffects_[layer].Get()))) return hr;
        // The header goes on top and pages at the bottom, whatever order they are made in.
        if (FAILED(hr = content_->AddVisual(visual.Get(), layer == kHeaderLayer ? FALSE : TRUE, nullptr))) return hr;
    }
    ComPtr<IDCompositionSurface> created;
    if (FAILED(hr = device_->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &created))) return hr;
    if (FAILED(hr = visual->SetContent(created.Get()))) return hr;
    *surface = created.Detach();
    return S_OK;
}

HRESULT Compositor::RemoveLayer(int layer) {
    if (layer < 0 || layer >= kLayerCount || !layers_[layer]) return S_OK;
    HRESULT hr = content_->RemoveVisual(layers_[layer].Get());
    layers_[layer].Reset();
    layerEffects_[layer].Reset();
    for (const auto& remaining : layers_) {
        if (remaining) return FAILED(hr) ? hr : device_->Commit();
    }
    // The last layer is gone, so the container and its reveal effect go too.
    const HRESULT removed = body_->RemoveVisual(content_.Get());
    content_.Reset();
    contentEffect_.Reset();
    if (FAILED(hr)) return hr;
    return FAILED(removed) ? removed : device_->Commit();
}

HRESULT Compositor::AnimateLayer(int layer, const ContentFade& opacity, const ContentFade& offsetX) {
    if (layer < 0 || layer >= kLayerCount || !layers_[layer]) return S_OK;
    ComPtr<IDCompositionAnimation> fade, slide;
    HRESULT hr;
    if (FAILED(hr = BuildFade(opacity, &fade))) return hr;
    if (FAILED(hr = BuildFade(offsetX, &slide))) return hr;
    if (FAILED(hr = layerEffects_[layer]->SetOpacity(fade.Get()))) return hr;
    return layers_[layer]->SetOffsetX(slide.Get());
}

HRESULT Compositor::SetLayer(int layer, float opacity, float offsetX) {
    if (layer < 0 || layer >= kLayerCount || !layers_[layer]) return S_OK;
    HRESULT hr = layerEffects_[layer]->SetOpacity(opacity);
    return FAILED(hr) ? hr : layers_[layer]->SetOffsetX(offsetX);
}

HRESULT Compositor::BuildFade(const ContentFade& fade, IDCompositionAnimation** animation) {
    ComPtr<IDCompositionAnimation> result;
    HRESULT hr = device_->CreateAnimation(&result);
    if (FAILED(hr)) return hr;
    // Holds the start value through the delay, then runs the exact ease-out cubic.
    if (fade.delay > 0 && FAILED(hr = result->AddCubic(0, fade.from, 0, 0, 0))) return hr;
    const CubicSegment s = fade.Curve();
    if (FAILED(hr = result->AddCubic(s.offset, s.c0, s.c1, s.c2, s.c3))) return hr;
    if (FAILED(hr = result->End(fade.delay + fade.duration, fade.to))) return hr;
    *animation = result.Detach();
    return S_OK;
}

HRESULT Compositor::AnimateContent(const ContentFade& opacity, const ContentFade& offsetY) {
    if (!content_) return S_OK;
    HRESULT hr;
    if (!contentEffect_) {
        if (FAILED(hr = device_->CreateEffectGroup(&contentEffect_))) return hr;
        if (FAILED(hr = content_->SetEffect(contentEffect_.Get()))) return hr;
    }
    ComPtr<IDCompositionAnimation> fade, slide;
    if (FAILED(hr = BuildFade(opacity, &fade))) return hr;
    if (FAILED(hr = BuildFade(offsetY, &slide))) return hr;
    if (FAILED(hr = contentEffect_->SetOpacity(fade.Get()))) return hr;
    return content_->SetOffsetY(slide.Get());
}

// Replacing the animations with constants lets DWM release them. Does not commit.
HRESULT Compositor::SettleContent(float opacity, float offsetY) {
    if (!content_ || !contentEffect_) return S_OK;
    HRESULT hr = contentEffect_->SetOpacity(opacity);
    return FAILED(hr) ? hr : content_->SetOffsetY(offsetY);
}


}  // namespace noitch
