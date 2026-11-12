#include "shelf_scene.h"
#include "island/island_page.h"
namespace noitch {
namespace {
D2D1_COLOR_F White(float a) { return {1, 1, 1, a}; }
D2D1_COLOR_F Blue(float a = 1) { return {0.46f, 0.68f, 1, a}; }
D2D1_COLOR_F TypeColor(ShelfFileType type, float a = 1) { const ShelfColor c = ShelfTypeColor(type); return {c.r, c.g, c.b, a}; }
D2D1_COLOR_F Lighter(D2D1_COLOR_F c, float amount) { return {c.r + (1 - c.r) * amount, c.g + (1 - c.g) * amount, c.b + (1 - c.b) * amount, c.a}; }
// Segoe MDL2 Assets, as elsewhere in the app.
constexpr wchar_t kPause[] = L"\xE769", kPlay[] = L"\xE768", kDelete[] = L"\xE74D", kBack[] = L"\xE76B", kForward[] = L"\xE76C",
                  kPin[] = L"\xE718", kClose[] = L"\xE711", kDrop[] = L"\xE896", kPaste[] = L"\xE77F", kPicture[] = L"\xE8B9";
// On a 24 grid: a page with a folded corner, its fold, and a folder's back and front.
const std::vector<LogoCommand> kPage = {{'M', {4, 1.5f}}, {'L', {15, 1.5f}}, {'L', {20, 6.5f}}, {'L', {20, 22.5f}}, {'L', {4, 22.5f}}, {'Z', {}}};
const std::vector<LogoCommand> kFold = {{'M', {15, 1.5f}}, {'L', {15, 6.5f}}, {'L', {20, 6.5f}}, {'Z', {}}};
const std::vector<LogoCommand> kFolderBack = {{'M', {2, 5}}, {'L', {9, 5}}, {'L', {11, 7.5f}}, {'L', {22, 7.5f}}, {'L', {22, 21}}, {'L', {2, 21}}, {'Z', {}}};
const std::vector<LogoCommand> kFolderFront = {{'M', {2, 10}}, {'L', {22, 10}}, {'L', {22, 21}}, {'L', {2, 21}}, {'Z', {}}};
}

HRESULT ShelfScene::Create(Compositor& compositor, Painter& painter, ShelfLayout layout, float left) {
    compositor_ = &compositor; painter_ = &painter; layout_ = layout;
    const float s = layout.Scale();
    constexpr auto lead = DWRITE_TEXT_ALIGNMENT_LEADING;
    constexpr auto center = DWRITE_TEXT_ALIGNMENT_CENTER;
    struct Format { IDWriteTextFormat** target; const wchar_t* family; float size; DWRITE_FONT_WEIGHT weight; DWRITE_TEXT_ALIGNMENT align; bool wrap; };
    const Format formats[] = {
        {&segment_, L"Segoe UI", 11, DWRITE_FONT_WEIGHT_SEMI_BOLD, center, false},
        {&status_, L"Segoe UI", 10, DWRITE_FONT_WEIGHT_MEDIUM, center, false},
        {&empty_, L"Segoe UI", 12, DWRITE_FONT_WEIGHT_SEMI_BOLD, center, false},
        {&name_, L"Segoe UI", 10, DWRITE_FONT_WEIGHT_MEDIUM, center, true},
        {&cardText_, L"Segoe UI", 11, DWRITE_FONT_WEIGHT_MEDIUM, lead, true},
        {&cardTitle_, L"Segoe UI", 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, lead, false},
        {&cardSmall_, L"Segoe UI", 10, DWRITE_FONT_WEIGHT_NORMAL, lead, true},
        {&kind_, L"Segoe UI", 9, DWRITE_FONT_WEIGHT_BOLD, lead, false},
        {&typeLabel_, L"Segoe UI", 9, DWRITE_FONT_WEIGHT_BLACK, center, false},
        {&count_, L"Segoe UI", 13, DWRITE_FONT_WEIGHT_BLACK, center, false},
        {&icon_, L"Segoe MDL2 Assets", 11, DWRITE_FONT_WEIGHT_NORMAL, center, false},
        {&bigIcon_, L"Segoe MDL2 Assets", 20, DWRITE_FONT_WEIGHT_NORMAL, center, false},
    };
    HRESULT hr;
    for (const Format& f : formats) {
        if (FAILED(hr = painter.MakeFormat(f.family, f.size * s, f.weight, f.align, f.target))) return hr;
        if (f.wrap) {
            (*f.target)->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            (*f.target)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }
    return compositor.CreateLayerSurface(PageLayer(IslandPage::Shelf), static_cast<UINT>(layout.Width()), static_cast<UINT>(layout.Height()), left, &surface_);
}

void ShelfScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveLayer(PageLayer(IslandPage::Shelf));
    surface_.Reset();
    for (auto* format : {&segment_, &status_, &empty_, &name_, &cardText_, &cardTitle_, &cardSmall_, &kind_, &typeLabel_, &count_, &icon_, &bigIcon_}) format->Reset();
}

void ShelfScene::PaintToolbar(bool clipboard, bool recording, size_t offset, size_t count, size_t total, const std::string& message) {
    const float s = layout_.Scale();
    const HomeRect files = layout_.Button(0), copies = layout_.Button(1);
    const HomeRect track{files.x, files.y, copies.Right() - files.x, files.height};
    painter_->Fill(track, track.height / 2, White(0.06f));
    HomeRect selected = clipboard ? copies : files;
    selected = {selected.x + 2 * s, selected.y + 2 * s, selected.width - 4 * s, selected.height - 4 * s};
    painter_->Fill(selected, selected.height / 2, White(0.16f));
    painter_->Text("Files", segment_.Get(), files, White(clipboard ? 0.55f : 1));
    painter_->Text("Clipboard", segment_.Get(), copies, White(clipboard ? 1 : 0.55f));
    struct Control { int index; const wchar_t* glyph; bool enabled; bool highlighted; };
    std::vector<Control> controls = {{3, kDelete, total > 0, false}, {4, kBack, offset > 0, false}, {5, kForward, offset + count < total, false}};
    if (clipboard) controls.insert(controls.begin(), {2, recording ? kPause : kPlay, true, !recording});
    for (const Control& control : controls) {
        const HomeRect box = layout_.Button(control.index);
        painter_->Fill(box, box.height / 2, control.highlighted ? Blue(0.3f) : White(0.06f));
        painter_->Glyph(control.glyph, icon_.Get(), box, White(control.enabled ? 0.85f : 0.25f));
    }
    std::string status = message;
    if (status.empty() && clipboard && !recording) status = "Paused";
    else if (status.empty() && count > 0 && (total > count || offset > 0))
        status = std::to_string(offset + 1) + "\xE2\x80\x93" + std::to_string(offset + count) + " of " + std::to_string(total);
    painter_->Text(status, status_.Get(), layout_.Status(), message.empty() ? White(0.4f) : Blue(0.9f));
}

// A dashed tray that invites a drop, lighting up while something is dragged over.
void ShelfScene::PaintEmpty(bool clipboard, bool dropping) {
    const float s = layout_.Scale();
    const HomeRect tray = layout_.Tray();
    painter_->Fill(tray, 16 * s, dropping ? Blue(0.12f) : White(0.025f));
    painter_->Outline(tray, 16 * s, 1.5f * s, dropping ? Blue() : White(0.2f), dropping ? 0 : 5 * s);
    const float midY = tray.y + tray.height / 2;
    painter_->Glyph(clipboard ? kPaste : kDrop, bigIcon_.Get(), {tray.x, midY - 26 * s, tray.width, 26 * s}, dropping ? Blue() : White(0.35f));
    const char* line = dropping ? "Release to drop it here" : clipboard ? "Copy something and it lands here" : "Drop files here, or onto the notch";
    painter_->Text(line, empty_.Get(), {tray.x, midY + 6 * s, tray.width, 16 * s}, White(dropping ? 1 : 0.55f));
}

// A page with a folded corner in the type's colour and its extension printed on it, a folder for a
// folder, or a little stack of pages for several files.
void ShelfScene::PaintFileGlyph(const std::vector<std::string>& paths, const HomeRect& rect) {
    const float s = layout_.Scale();
    const std::string first = paths.empty() ? "" : paths.front();
    const ShelfFileType type = ShelfFileTypeOf(first);
    const D2D1_COLOR_F color = TypeColor(type);
    if (paths.size() <= 1 && type == ShelfFileType::Folder) {
        painter_->Shape(kFolderBack, rect, color);
        painter_->Shape(kFolderFront, rect, Lighter(color, 0.25f));
        return;
    }
    if (paths.size() > 1) {
        painter_->Shape(kPage, {rect.x + 5 * s, rect.y - 4 * s, rect.width, rect.height}, TypeColor(type, 0.35f));
        painter_->Shape(kPage, {rect.x + 2.5f * s, rect.y - 2 * s, rect.width, rect.height}, TypeColor(type, 0.6f));
    }
    painter_->Shape(kPage, rect, color);
    painter_->Shape(kFold, rect, White(0.45f));
    const std::string label = paths.size() > 1 ? std::to_string(paths.size()) : ShelfTypeLabel(first);
    painter_->Text(label.empty() ? "FILE" : label, paths.size() > 1 ? count_.Get() : typeLabel_.Get(),
                   {rect.x, rect.y + rect.height / 2 - 2 * s, rect.width, 14 * s}, White(1));
}

// One file, Boring Notch style: a big glyph or thumbnail with the name under it.
void ShelfScene::PaintTile(const ShelfItem& item, const ShelfThumbnail& thumbnail, int slot, bool hovered) {
    const float s = layout_.Scale();
    const HomeRect tile = layout_.Card(slot, false);
    if (hovered) painter_->Fill(tile, 14 * s, White(0.08f));
    const HomeRect glyph = layout_.Glyph(slot);
    if (!thumbnail.pixels.empty()) {
        const float factor = std::min(glyph.width / thumbnail.width, glyph.height / thumbnail.height);
        const float w = thumbnail.width * factor, h = thumbnail.height * factor;
        painter_->Bitmap(thumbnail.pixels.data(), thumbnail.width, thumbnail.height, {glyph.x + (glyph.width - w) / 2, glyph.y + (glyph.height - h) / 2, w, h});
    } else {
        PaintFileGlyph(item.files.empty() ? std::vector<std::string>{item.title} : item.files, glyph);
    }
    painter_->Text(item.title, name_.Get(), layout_.Name(slot), White(0.85f));
    PaintBubbles(item, slot, false, hovered);
}

// One copied thing: text as text, a link as its site, an image as itself.
void ShelfScene::PaintCard(const ShelfItem& item, const ShelfThumbnail& thumbnail, int slot, bool hovered) {
    const float s = layout_.Scale();
    const HomeRect card = layout_.Card(slot, true);
    painter_->Fill(card, 14 * s, White(hovered ? 0.11f : 0.06f));
    const HomeRect inner{card.x + 10 * s, card.y + 10 * s, card.width - 20 * s, card.height - 32 * s};
    std::string kind = "Text";
    switch (item.kind) {
        case ShelfKind::Image:
            kind = "Image";
            if (!thumbnail.pixels.empty()) {
                const float factor = std::min(inner.width / thumbnail.width, inner.height / thumbnail.height);
                const float w = thumbnail.width * factor, h = thumbnail.height * factor;
                painter_->Bitmap(thumbnail.pixels.data(), thumbnail.width, thumbnail.height, {inner.x + (inner.width - w) / 2, inner.y + (inner.height - h) / 2, w, h});
            } else {
                painter_->Glyph(kPicture, bigIcon_.Get(), inner, White(0.4f));
            }
            break;
        case ShelfKind::Link:
            kind = "Link";
            painter_->Text(item.title, cardTitle_.Get(), {inner.x, inner.y, inner.width, 17 * s}, White(1));
            painter_->Text(item.preview, cardSmall_.Get(), {inner.x, inner.y + 20 * s, inner.width, inner.height - 20 * s}, White(0.5f));
            break;
        case ShelfKind::Files:
            kind = item.files.size() > 1 ? std::to_string(item.files.size()) + " files" : "File";
            PaintFileGlyph(item.files, {inner.x, inner.y, 26 * s, 30 * s});
            painter_->Text(item.title, cardText_.Get(), {inner.x, inner.y + 34 * s, inner.width, inner.height - 34 * s}, White(1));
            break;
        default:
            painter_->Text(item.preview, cardText_.Get(), inner, White(0.88f));
            break;
    }
    painter_->Text(kind, kind_.Get(), {card.x + 14 * s, card.Bottom() - 19 * s, card.width - 60 * s, 12 * s}, item.kind == ShelfKind::Link ? Blue() : White(0.5f));
    PaintBubbles(item, slot, true, hovered);
}

// Pinned items wear a small pin; hovering shows pin and remove as round bubbles.
void ShelfScene::PaintBubbles(const ShelfItem& item, int slot, bool clipboard, bool hovered) {
    const HomeRect pin = layout_.PinBubble(slot, clipboard);
    if (hovered) {
        const HomeRect remove = layout_.RemoveBubble(slot, clipboard);
        painter_->Fill(pin, pin.height / 2, {0.18f, 0.18f, 0.18f, 0.95f});
        painter_->Glyph(kPin, icon_.Get(), pin, item.pinned ? Blue() : White(1));
        painter_->Fill(remove, remove.height / 2, {0.18f, 0.18f, 0.18f, 0.95f});
        painter_->Glyph(kClose, icon_.Get(), remove, White(1));
    } else if (item.pinned) {
        painter_->Glyph(kPin, icon_.Get(), pin, Blue());
    }
}

HRESULT ShelfScene::Paint(const std::vector<ShelfItem>& items, const std::vector<ShelfThumbnail>& thumbnails, bool clipboard, bool recording, size_t offset, size_t total, int hovered, const std::string& message, bool dropping) {
    if (!surface_) return E_FAIL;
    HRESULT hr = painter_->Begin(surface_.Get(), layout_.Width(), layout_.Height());
    if (FAILED(hr)) return hr;
    PaintToolbar(clipboard, recording, offset, items.size(), total, message);
    if (items.empty() || dropping) {
        PaintEmpty(clipboard, dropping);
    } else {
        static const ShelfThumbnail none;
        for (size_t i = 0; i < items.size() && i < static_cast<size_t>(ShelfLayout::PerPage(clipboard)); ++i) {
            const ShelfThumbnail& thumbnail = i < thumbnails.size() ? thumbnails[i] : none;
            const bool over = hovered == static_cast<int>(i);
            if (clipboard) PaintCard(items[i], thumbnail, static_cast<int>(i), over);
            else PaintTile(items[i], thumbnail, static_cast<int>(i), over);
        }
    }
    if (FAILED(hr = painter_->End(surface_.Get()))) return hr;
    return compositor_->Commit();
}
}
