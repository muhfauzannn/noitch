#pragma once

#include <d3d11.h>
#include <dcomp.h>
#include <windows.h>
#include <wrl/client.h>

#include "opacity_motion.h"
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

    // A surface for what sits on the pill. Its visual is inside the pill's clip, so transitions
    // reveal and hide it in DWM with nothing else to animate. `left` is the expanded pill's left edge.
    HRESULT CreateContentSurface(UINT width, UINT height, float left, IDCompositionSurface** surface);
    HRESULT RemoveContent();

    // Opacity of the content and of the hover plate beneath it, ramped by DWM. These need
    // IDCompositionVisual3 (Windows 10 1607). Without it they do nothing, and the plate isn't created.
    HRESULT AnimateContentOpacity(const OpacityMotion& motion);
    HRESULT CreatePlateSurface(UINT size, IDCompositionSurface** surface);
    HRESULT SetPlateOffset(float x, float y);
    HRESULT AnimatePlateOpacity(const OpacityMotion& motion);
    HRESULT Commit() { return device_->Commit(); }

    // For Direct2D, which draws on the same device as the composition surfaces.
    IDXGIDevice* DxgiDevice() const { return dxgi_.Get(); }

    // Rises when the tree is rebuilt. Anything made from the old device must then be made again.
    int Generation() const { return generation_; }

private:
    HRESULT BuildScene();
    HRESULT BuildFlare(IDCompositionVisual* visual, bool mirrored);
    HRESULT Paint(IDCompositionSurface* surface, UINT width, UINT height, const void* pixels);
    HRESULT AnimateOpacity(IDCompositionVisual3* visual, const OpacityMotion& motion);
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
    Microsoft::WRL::ComPtr<IDCompositionVisual3> content3_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> plate_;
    Microsoft::WRL::ComPtr<IDCompositionVisual3> plate3_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> leftFlare_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> rightFlare_;
    Microsoft::WRL::ComPtr<IDCompositionRectangleClip> clip_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
};

}  // namespace noitch
