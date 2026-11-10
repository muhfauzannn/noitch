#pragma once

#include <dwrite.h>
#include <wrl/client.h>

#include "island/compositor.h"
#include "island/painter.h"
#include "usage_layout.h"
#include "usage_model.h"

namespace noitch {

// Draws the usage page into one surface: a column per tool. It decides nothing, and draws only
// when the text changes, at most once a minute while shown.
class UsageScene {
public:
    HRESULT Create(Compositor& compositor, Painter& painter, const UsageLayout& layout, float left, float scale);
    void Release();
    bool Exists() const { return surface_ != nullptr; }

    HRESULT Paint(const UsageModel& model);

private:
    void PaintColumn(int index, const UsageColumn& column);

    Compositor* compositor_ = nullptr;
    Painter* painter_ = nullptr;
    UsageLayout layout_;
    UsageModel shown_;
    bool hasShown_ = false;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> tokensFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> costFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallRightFormat_;
};

}  // namespace noitch
