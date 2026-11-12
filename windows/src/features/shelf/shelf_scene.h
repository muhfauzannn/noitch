#pragma once
#include "island/compositor.h"
#include "island/painter.h"
#include "shelf_model.h"
#include "shelf_thumbnail.h"
namespace noitch {
// Shelf's surface: a slim toolbar, then a tray of file tiles or clipboard cards. It decides nothing
// and draws only when a value changes. Matches ShelfScene.swift.
class ShelfScene {
public:
    HRESULT Create(Compositor& compositor, Painter& painter, ShelfLayout layout, float left);
    void Release();
    bool Exists() const { return surface_ != nullptr; }
    const ShelfLayout& Layout() const { return layout_; }
    // `thumbnails` holds one entry per visible item; an empty one means none.
    HRESULT Paint(const std::vector<ShelfItem>& items, const std::vector<ShelfThumbnail>& thumbnails, bool clipboard, bool recording, size_t offset, size_t total, int hovered, const std::string& message, bool dropping);
private:
    void PaintToolbar(bool clipboard, bool recording, size_t offset, size_t count, size_t total, const std::string& message);
    void PaintEmpty(bool clipboard, bool dropping);
    void PaintTile(const ShelfItem& item, const ShelfThumbnail& thumbnail, int slot, bool hovered);
    void PaintCard(const ShelfItem& item, const ShelfThumbnail& thumbnail, int slot, bool hovered);
    void PaintFileGlyph(const std::vector<std::string>& paths, const HomeRect& rect);
    void PaintBubbles(const ShelfItem& item, int slot, bool clipboard, bool hovered);
    Compositor* compositor_ = nullptr;
    Painter* painter_ = nullptr;
    ShelfLayout layout_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> segment_, status_, empty_, name_, cardText_, cardTitle_, cardSmall_, kind_, typeLabel_, count_, icon_, bigIcon_;
};
}
