#pragma once

#include <d3d11.h>
#include <dcomp.h>
#include <windows.h>
#include <wrl/client.h>

#include "spring.h"

namespace noitch {

// Owns the DirectComposition visual tree. The pill is a rounded clip over a stretched 1x1 black
// surface, so moving it costs no drawing: only clip properties change, and DWM animates those.
class Compositor {
public:
    HRESULT Create(HWND hwnd);

    // Sets the pill with no animation and drops any running one.
    HRESULT ShowPill(const Pill& pill);

    // Hands a whole transition to DWM. This process does no work until it is over.
    HRESULT AnimatePill(const PillMotion& motion);

    // Rebuilds the tree if the GPU device was lost. Call on display changes, never on a timer.
    HRESULT RecoverIfLost(const Pill& pill);

private:
    HRESULT BuildScene();
    HRESULT PaintFill(IDCompositionSurface* surface, ID3D11DeviceContext* context);
    HRESULT BuildAnimation(const PillMotion& motion, int channel, IDCompositionAnimation** animation);

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<IDCompositionDevice> device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> fill_;
    Microsoft::WRL::ComPtr<IDCompositionRectangleClip> clip_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
};

}  // namespace noitch
