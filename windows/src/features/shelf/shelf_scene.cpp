#include "shelf_scene.h"
#include "island/island_page.h"
namespace noitch {
namespace {
D2D1_COLOR_F White(float a) { return {1, 1, 1, a}; }
D2D1_COLOR_F Blue(float a = 1) { return {0.46f, 0.68f, 1, a}; }
}
HRESULT ShelfScene::Create(Compositor& compositor, Painter& painter, ShelfLayout layout, float left) {
    compositor_ = &compositor; painter_ = &painter; layout_ = layout;
    const float scale = layout.Scale();
    HRESULT hr = painter.MakeFormat(L"Segoe UI", 12 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, &title_);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, &label_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 10 * scale, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, &small_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 15 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, &message_))) return hr;
    return compositor.CreateLayerSurface(PageLayer(IslandPage::Shelf), static_cast<UINT>(layout.Width()), static_cast<UINT>(layout.Height()), left, &surface_);
}
void ShelfScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveLayer(PageLayer(IslandPage::Shelf));
    surface_.Reset(); title_.Reset(); label_.Reset(); small_.Reset(); message_.Reset();
}
HRESULT ShelfScene::Paint(const std::vector<ShelfItem>& items, const std::vector<ShelfThumbnail>& thumbnails, bool clipboard, bool recording, size_t offset, size_t total, int hovered, const std::string& message, bool dropping) {
    if (!surface_) return E_FAIL;
    HRESULT hr = painter_->Begin(surface_.Get(), layout_.Width(), layout_.Height());
    if (FAILED(hr)) return hr;
    const float s = layout_.Scale();
    const std::string buttons[] = {"Files", "Clipboard", recording ? "Pause" : "Resume", "Clear", "<", ">"};
    for (int i = 0; i < 6; ++i) {
        if (i == 2 && !clipboard) continue;
        const auto box = layout_.Button(i);
        const bool selected = i == (clipboard ? 1 : 0);
        painter_->Fill(box, 8 * s, White(selected ? 0.13f : 0.035f));
        painter_->Text(buttons[i], label_.Get(), {box.x + 10 * s, box.y + 4 * s, box.width - 14 * s, 18 * s}, White(selected ? 1 : 0.65f));
    }
    if (items.empty() || dropping) {
        auto box = layout_.Card(0); box.width = layout_.Width() - 44 * s;
        painter_->Fill(box, 12 * s, Blue(dropping ? 0.13f : 0.025f));
        painter_->Text(dropping ? "Release to keep it here" : clipboard ? "Your next copy starts here" : "A little space between apps", message_.Get(), {box.x + 18 * s, box.y + 14 * s, box.width - 36 * s, 22 * s}, White(1));
        painter_->Text(clipboard ? "Copy text, a link, an image or a file. Click a card to copy it back." : "Drop files onto the notch. Pick them up again whenever you need them.", label_.Get(), {box.x + 18 * s, box.y + 43 * s, box.width - 36 * s, 22 * s}, White(0.45f));
    } else for (size_t i = 0; i < items.size() && i < 3; ++i) {
        const auto& item = items[i]; const auto card = layout_.Card(static_cast<int>(i));
        painter_->Fill(card, 12 * s, Blue(hovered == static_cast<int>(i) ? 0.65f : 0.13f));
        const HomeRect inner{card.x + s, card.y + s, card.width - 2 * s, card.height - 2 * s};
        painter_->Fill(inner, 11 * s, D2D1::ColorF(0, 0, 0, 1));
        painter_->Fill(inner, 11 * s, White(hovered == static_cast<int>(i) ? 0.105f : 0.055f));
        const std::string badge = item.kind == ShelfKind::Files ? "FILES" : item.kind == ShelfKind::Image ? "IMAGE" : item.kind == ShelfKind::Link ? "LINK" : "TEXT";
        painter_->Text(badge, small_.Get(), {card.x + 12 * s, card.y + 9 * s, card.width - 68 * s, 13 * s}, Blue());
        painter_->Text(item.title, title_.Get(), {card.x + 12 * s, card.y + 27 * s, card.width - 24 * s, 17 * s}, White(1));
        if (i < thumbnails.size() && !thumbnails[i].pixels.empty()) {
            const auto& thumbnail = thumbnails[i];
            const float factor = std::min(43.0f / thumbnail.width, 42.0f / thumbnail.height) * s;
            painter_->Bitmap(thumbnail.pixels.data(), thumbnail.width, thumbnail.height,
                             {card.Right() - 55 * s, card.y + 24 * s, thumbnail.width * factor, thumbnail.height * factor});
        } else {
        painter_->Text(item.preview, small_.Get(), {card.x + 12 * s, card.y + 48 * s, card.width - 24 * s, 24 * s}, White(0.45f));
        }
        if (hovered == static_cast<int>(i) || item.pinned) painter_->Text(item.pinned ? "*" : "+", label_.Get(), {card.Right() - 49 * s, card.y + 5 * s, 18 * s, 18 * s}, Blue());
        if (hovered == static_cast<int>(i)) painter_->Text("x", label_.Get(), {card.Right() - 24 * s, card.y + 5 * s, 18 * s, 18 * s}, White(0.6f));
    }
    const std::string footer = !message.empty() ? message : total ? std::to_string(offset + 1) + "-" + std::to_string(std::min(offset + 3, total)) + " of " + std::to_string(total) + "  |  Click to copy  |  Drag to another app" : clipboard ? "History stays on this PC. Pause whenever you need." : "Files stay in their original location.";
    painter_->Text(footer, small_.Get(), layout_.Footer(), White(0.4f));
    if (FAILED(hr = painter_->End(surface_.Get()))) return hr;
    return compositor_->Commit();
}
}
