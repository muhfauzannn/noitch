#pragma once

#include <dwrite.h>
#include <wrl/client.h>

#include "island/compositor.h"
#include "island/painter.h"
#include "usage_layout.h"
#include "usage_model.h"

namespace noitch {

// Draws the usage page into one surface: per tool, a ring gauge (five hours inside, the week
// outside, a dot on each marking how much of its time has passed) and beside it the numbers and the
// one thing worth knowing. It decides nothing, and draws only when the model changes.
class UsageScene {
public:
    HRESULT Create(Compositor& compositor, Painter& painter, const UsageLayout& layout, float left, float scale);
    void Release();
    bool Exists() const { return surface_ != nullptr; }

    HRESULT Paint(const UsageModel& model);

private:
    void PaintTool(int index, const UsageModel::Tool& tool);

    Compositor* compositor_ = nullptr;
    Painter* painter_ = nullptr;
    UsageLayout layout_;
    UsageModel shown_;
    bool hasShown_ = false;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> centerFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> captionFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> nameFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallBoldFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallRightFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> paceFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> headlineFormat_;
};

}  // namespace noitch
