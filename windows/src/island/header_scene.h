#pragma once

#include <dwrite.h>
#include <wrl/client.h>

#include "compositor.h"
#include "island_page.h"
#include "painter.h"

namespace noitch {

// Draws the header every page shares: page tabs with a plate behind the current one, and settings.
// Its layer sits above the pages, so it stays put while they slide. It draws only on a change.
class HeaderScene {
public:
    HRESULT Create(Compositor& compositor, Painter& painter, const HeaderLayout& layout, float width, float height, float left, float scale);
    void Release();
    bool Exists() const { return surface_ != nullptr; }
    const HeaderLayout& Layout() const { return layout_; }

    // Commits, so animations set on other layers just before start on the same frame.
    HRESULT Paint(IslandPage page, HeaderControl hovered, bool isPressed);

private:
    Compositor* compositor_ = nullptr;
    Painter* painter_ = nullptr;
    HeaderLayout layout_;
    float width_ = 0;
    float height_ = 0;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat_;
};

}  // namespace noitch
