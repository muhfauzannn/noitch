#include "compositor.h"

#include <cstdint>

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
    fill_.Reset();
    clip_.Reset();
    surface_.Reset();
    device_.Reset();

    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED,
                                   nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, &context);
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(hr = d3d.As(&dxgi))) return hr;
    if (FAILED(hr = DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&device_)))) return hr;
    if (FAILED(hr = device_->CreateTargetForHwnd(hwnd_, TRUE, &target_))) return hr;
    if (FAILED(hr = device_->CreateVisual(&root_))) return hr;
    if (FAILED(hr = device_->CreateVisual(&fill_))) return hr;
    if (FAILED(hr = device_->CreateRectangleClip(&clip_))) return hr;
    if (FAILED(hr = device_->CreateSurface(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface_))) return hr;
    if (FAILED(hr = PaintFill(surface_.Get(), context.Get()))) return hr;

    D2D_MATRIX_3X2_F stretch{};
    stretch._11 = kFillScale;
    stretch._22 = kFillScale;
    if (FAILED(hr = fill_->SetContent(surface_.Get()))) return hr;
    if (FAILED(hr = fill_->SetTransform(stretch))) return hr;
    if (FAILED(hr = fill_->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR))) return hr;

    if (FAILED(hr = root_->SetBorderMode(DCOMPOSITION_BORDER_MODE_SOFT))) return hr;
    if (FAILED(hr = root_->SetClip(clip_.Get()))) return hr;
    if (FAILED(hr = root_->AddVisual(fill_.Get(), FALSE, nullptr))) return hr;
    return target_->SetRoot(root_.Get());
}

HRESULT Compositor::PaintFill(IDCompositionSurface* surface, ID3D11DeviceContext* context) {
    ComPtr<IDXGISurface> dxgiSurface;
    POINT offset{};
    HRESULT hr = surface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurface), &offset);
    if (FAILED(hr)) return hr;

    // The surface can be a region of a shared atlas, so write only the pixel at `offset`.
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(hr = dxgiSurface.As(&texture))) {
        const UINT x = static_cast<UINT>(offset.x);
        const UINT y = static_cast<UINT>(offset.y);
        const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
        const std::uint32_t opaqueBlack = 0xFF000000;
        context->UpdateSubresource(texture.Get(), 0, &box, &opaqueBlack, sizeof(opaqueBlack), sizeof(opaqueBlack));
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
    return device_->Commit();
}

HRESULT Compositor::BuildAnimation(const PillMotion& motion, int channel, IDCompositionAnimation** animation) {
    ComPtr<IDCompositionAnimation> result;
    HRESULT hr = device_->CreateAnimation(&result);
    if (FAILED(hr)) return hr;

    for (int i = 0; i < motion.SegmentCount(); ++i) {
        const CubicSegment s = motion.Segment(channel, i);
        if (FAILED(hr = result->AddCubic(s.offset, s.c0, s.c1, s.c2, s.c3))) return hr;
    }
    if (FAILED(hr = result->End(motion.Duration(), static_cast<float>(motion.Channel(channel).Target())))) return hr;
    *animation = result.Detach();
    return S_OK;
}

HRESULT Compositor::AnimatePill(const PillMotion& motion) {
    ComPtr<IDCompositionAnimation> left, right, bottom, radius;
    HRESULT hr;
    if (FAILED(hr = BuildAnimation(motion, 0, &left))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 1, &right))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 2, &bottom))) return hr;
    if (FAILED(hr = BuildAnimation(motion, 3, &radius))) return hr;

    if (FAILED(hr = clip_->SetLeft(left.Get()))) return hr;
    if (FAILED(hr = clip_->SetRight(right.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottom(bottom.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomLeftRadiusX(radius.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomLeftRadiusY(radius.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomRightRadiusX(radius.Get()))) return hr;
    if (FAILED(hr = clip_->SetBottomRightRadiusY(radius.Get()))) return hr;
    return device_->Commit();
}

HRESULT Compositor::RecoverIfLost(const Pill& pill) {
    BOOL valid = FALSE;
    HRESULT hr = device_->CheckDeviceState(&valid);
    if (SUCCEEDED(hr) && valid) return S_OK;
    if (FAILED(hr = BuildScene())) return hr;
    return ShowPill(pill);
}

}  // namespace noitch
