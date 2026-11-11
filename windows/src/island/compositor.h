#pragma once

#include <d3d11.h>
#include <dcomp.h>
#include <windows.h>
#include <wrl/client.h>

#include "spring.h"

namespace noitch {

// Owns the DirectComposition visual tree. The pill is a rounded clip over a stretched 1x1 black
// surface and the flares are small fixed surfaces, so moving them costs no drawing: only clip and
// offset properties change, and DWM animates those.
class Compositor {
public:
    HRESULT Create(HWND hwnd);

    // Rebuilds the flares for a new DPI. Follow with ShowPill, which commits.
    HRESULT SetFlareRadius(int radius);

    // Sets the pill with no animation and drops any running one.
    HRESULT ShowPill(const Pill& pill);

    // Hands a whole transition to DWM. This process does no work until it is over.
    HRESULT AnimatePill(const PillMotion& motion);

    // Rebuilds the tree if the GPU device was lost. Call on display changes, never on a timer.
    HRESULT RecoverIfLost(const Pill& pill);

    // What sits on the pill is a container inside the pill's clip, holding one visual per layer:
    // the shared header above the pages. `left` is the expanded pill's left edge.
    static constexpr int kHeaderLayer = 0;
    static constexpr int kLayerCount = 1 + 3;  // The header, then one per page.
    HRESULT CreateLayerSurface(int layer, UINT width, UINT height, float left, IDCompositionSurface** surface);
    HRESULT RemoveLayer(int layer);
    // Fades and slides the whole container: the open reveal. Does not commit: the pill transition
    // that follows commits both, so they start on the same DWM frame. No-op until content exists.
    HRESULT AnimateContent(const ContentFade& opacity, const ContentFade& offsetY);
    HRESULT SettleContent(float opacity, float offsetY);
    // Fades and slides one layer sideways: a page switch. Does not commit.
    HRESULT AnimateLayer(int layer, const ContentFade& opacity, const ContentFade& offsetX);
    HRESULT SetLayer(int layer, float opacity, float offsetX);
    HRESULT Commit() { return device_->Commit(); }

    // The closed island's music sits in the pill's clip beside the content container, so it shows
    // while the island is closed. LiveScene builds the visual from the device; these place it.
    IDCompositionDevice* Device() const { return device_.Get(); }
    HRESULT AttachLive(IDCompositionVisual* visual);
    HRESULT DetachLive(IDCompositionVisual* visual);
    // Uploads premultiplied BGRA pixels into a whole surface.
    HRESULT Upload(IDCompositionSurface* surface, UINT width, UINT height, const void* pixels) { return Paint(surface, width, height, pixels); }
    // An eased fade as a DirectComposition animation, for any animatable property.
    HRESULT BuildFade(const ContentFade& fade, IDCompositionAnimation** animation);

    // For Direct2D, which draws on the same device as the composition surfaces.
    IDXGIDevice* DxgiDevice() const { return dxgi_.Get(); }

    // Rises when the tree is rebuilt. Anything made from the old device must then be made again.
    int Generation() const { return generation_; }

private:
    HRESULT BuildScene();
    HRESULT BuildFlare(IDCompositionVisual* visual, bool mirrored);
    HRESULT Paint(IDCompositionSurface* surface, UINT width, UINT height, const void* pixels);
    HRESULT BuildAnimation(const PillMotion& motion, int channel, float bias, IDCompositionAnimation** animation);

    HWND hwnd_ = nullptr;
    int flareRadius_ = 0;
    int generation_ = 0;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> body_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> fill_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> content_;
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> contentEffect_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> layers_[kLayerCount];
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> layerEffects_[kLayerCount];
    Microsoft::WRL::ComPtr<IDCompositionVisual> leftFlare_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> rightFlare_;
    Microsoft::WRL::ComPtr<IDCompositionRectangleClip> clip_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
};

}  // namespace noitch
