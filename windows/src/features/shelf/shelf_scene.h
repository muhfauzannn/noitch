#pragma once
#include "island/compositor.h"
#include "island/painter.h"
#include "shelf_model.h"
#include "shelf_thumbnail.h"
namespace noitch {
class ShelfScene {
public:
    HRESULT Create(Compositor& compositor, Painter& painter, ShelfLayout layout, float left);
    void Release();
    bool Exists() const { return surface_ != nullptr; }
    const ShelfLayout& Layout() const { return layout_; }
    HRESULT Paint(const std::vector<ShelfItem>& items, const std::vector<ShelfThumbnail>& thumbnails, bool clipboard, bool recording, size_t offset, size_t total, int hovered, const std::string& message, bool dropping);
private:
    Compositor* compositor_ = nullptr;
    Painter* painter_ = nullptr;
    ShelfLayout layout_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title_, label_, small_, message_;
};
}
