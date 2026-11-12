#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include "features/home/home_layout.h"

namespace noitch {
enum class ShelfKind : uint32_t { Files, Text, Link, Image };
struct ShelfItem {
    std::string id;
    ShelfKind kind = ShelfKind::Text;
    std::string title;
    std::string preview;
    std::vector<std::string> files;
    std::string payload;
    bool pinned = false;
    bool operator==(const ShelfItem&) const = default;
};
inline constexpr size_t kShelfCapacity = 50;
inline bool InsertShelfItem(std::vector<ShelfItem>& items, ShelfItem item) {
    const auto previous = std::find_if(items.begin(), items.end(), [&](const auto& old) { return old.id == item.id; });
    if (previous != items.end()) { item.pinned = previous->pinned; items.erase(previous); }
    if (items.size() >= kShelfCapacity) {
        const auto expendable = std::find_if(items.rbegin(), items.rend(), [](const auto& old) { return !old.pinned; });
        if (expendable == items.rend()) return false;
        items.erase(std::next(expendable).base());
    }
    items.insert(items.begin(), std::move(item));
    return true;
}
// What kind of file a path is, by its extension, for the tile's glyph and colour. A path without an
// extension is most often a folder. Matches ShelfFileType in ShelfModel.swift.
enum class ShelfFileType { Image, Pdf, Document, Sheet, Slides, Archive, Code, Media, Folder, Other };

inline std::string ShelfExtension(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || dot + 1 >= path.size()) return "";
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext;
}

inline ShelfFileType ShelfFileTypeOf(const std::string& path) {
    const std::string ext = ShelfExtension(path);
    if (ext.empty()) return ShelfFileType::Folder;
    struct Group { ShelfFileType type; const char* const* names; size_t count; };
    static const char* const image[] = {"png", "jpg", "jpeg", "gif", "heic", "webp", "tif", "tiff", "bmp", "svg", "raw"};
    static const char* const pdf[] = {"pdf"};
    static const char* const document[] = {"doc", "docx", "pages", "txt", "md", "rtf", "odt"};
    static const char* const sheet[] = {"xls", "xlsx", "csv", "numbers", "ods", "tsv"};
    static const char* const slides[] = {"ppt", "pptx", "key", "odp"};
    static const char* const archive[] = {"zip", "rar", "7z", "tar", "gz", "dmg", "iso", "pkg"};
    static const char* const code[] = {"swift", "c", "cpp", "h", "hpp", "m", "js", "ts", "tsx", "py", "rb", "go", "rs", "java", "kt", "json", "html", "css", "sh", "yml", "yaml", "xml"};
    static const char* const media[] = {"mp3", "wav", "m4a", "aac", "flac", "mp4", "mov", "mkv", "avi", "webm"};
    const Group groups[] = {
        {ShelfFileType::Image, image, std::size(image)}, {ShelfFileType::Pdf, pdf, std::size(pdf)},
        {ShelfFileType::Document, document, std::size(document)}, {ShelfFileType::Sheet, sheet, std::size(sheet)},
        {ShelfFileType::Slides, slides, std::size(slides)}, {ShelfFileType::Archive, archive, std::size(archive)},
        {ShelfFileType::Code, code, std::size(code)}, {ShelfFileType::Media, media, std::size(media)},
    };
    for (const Group& group : groups)
        for (size_t i = 0; i < group.count; ++i) if (ext == group.names[i]) return group.type;
    return ShelfFileType::Other;
}

// sRGB, shared with the macOS app.
struct ShelfColor { float r, g, b; };
inline ShelfColor ShelfTypeColor(ShelfFileType type) {
    switch (type) {
        case ShelfFileType::Image: return {0.69f, 0.45f, 1};
        case ShelfFileType::Pdf: return {1, 0.38f, 0.35f};
        case ShelfFileType::Document: return {0.36f, 0.6f, 1};
        case ShelfFileType::Sheet: return {0.3f, 0.8f, 0.5f};
        case ShelfFileType::Slides: return {1, 0.6f, 0.25f};
        case ShelfFileType::Archive: return {0.95f, 0.76f, 0.3f};
        case ShelfFileType::Code: return {0.3f, 0.78f, 0.85f};
        case ShelfFileType::Media: return {1, 0.42f, 0.7f};
        case ShelfFileType::Folder: return {0.48f, 0.64f, 0.9f};
        default: return {0.6f, 0.62f, 0.68f};
    }
}

// The label printed on a file's page: its extension, cut to four letters.
inline std::string ShelfTypeLabel(const std::string& path) {
    std::string ext = ShelfExtension(path).substr(0, 4);
    for (char& c : ext) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return ext;
}

// Pixels from the page's top-left. A slim toolbar under the header, then a tray: seven file tiles in
// Files, four cards in Clipboard. Matches ShelfLayout in ShelfModel.swift.
class ShelfLayout {
public:
    static constexpr float kSidePadding = 22, kGap = 8, kControl = 24, kSegment = 88, kBubble = 18;
    ShelfLayout(float width = 580, float height = 180, float header = 32, float scale = 1) : width_(width), height_(height), header_(header), scale_(scale) {}

    static int PerPage(bool clipboard) { return clipboard ? 4 : 7; }
    HomeRect Tray() const { return Px(LogicalTray()); }
    HomeRect Card(int slot, bool clipboard) const { return Px(LogicalCard(slot, clipboard)); }
    int SlotAt(float x, float y, bool clipboard) const {
        for (int slot = 0; slot < PerPage(clipboard); ++slot) if (Card(slot, clipboard).Contains(x, y)) return slot;
        return -1;
    }
    // A file's page or folder, centred near the tile's top; its name sits below.
    HomeRect Glyph(int slot) const { return Px(LogicalGlyph(slot)); }
    HomeRect Name(int slot) const {
        const HomeRect tile = LogicalCard(slot, false);
        const HomeRect glyph = LogicalGlyph(slot);
        return Px({tile.x + 3, glyph.Bottom() + 5, tile.width - 6, 26});
    }
    // A tile has pin at its top-left and remove at its top-right; a card has both at its bottom-right.
    HomeRect PinBubble(int slot, bool clipboard) const {
        const HomeRect tile = LogicalCard(slot, clipboard);
        if (clipboard) { HomeRect remove = LogicalRemove(slot, true); remove.x -= kBubble + 4; return Px(remove); }
        return Px({tile.x + 3, tile.y + 3, kBubble, kBubble});
    }
    HomeRect RemoveBubble(int slot, bool clipboard) const { return Px(LogicalRemove(slot, clipboard)); }
    // 0 Files and 1 Clipboard, one segmented control at the left; 2 pause, 3 clear, 4 back and
    // 5 forward, round buttons at the right.
    HomeRect Button(int index) const {
        const float y = header_ + 6;
        if (index <= 1) return Px({kSidePadding + index * kSegment, y, kSegment, kControl});
        const float right = width_ - kSidePadding;
        const float xs[] = {right - kControl * 4 - 6 * 2 - 14, right - kControl * 3 - 6 - 14, right - kControl * 2 - 6, right - kControl};
        return Px({xs[index - 2], y, kControl, kControl});
    }
    // Between the segments and the buttons: the page's position, or a short message.
    HomeRect Status() const {
        const float left = kSidePadding + kSegment * 2 + 12;
        const float right = width_ - kSidePadding - kControl * 4 - 6 * 2 - 14 - 12;
        return Px({left, header_ + 11, right - left, 14});
    }
    float Width() const { return width_ * scale_; }
    float Height() const { return height_ * scale_; }
    float Scale() const { return scale_; }

private:
    HomeRect LogicalTray() const { return {kSidePadding, header_ + 38, width_ - kSidePadding * 2, height_ - 10 - (header_ + 38)}; }
    HomeRect LogicalCard(int slot, bool clipboard) const {
        const HomeRect tray = LogicalTray();
        const float count = static_cast<float>(PerPage(clipboard));
        const float width = (tray.width - kGap * (count - 1)) / count;
        return {tray.x + static_cast<float>(slot) * (width + kGap), tray.y, width, tray.height};
    }
    HomeRect LogicalGlyph(int slot) const {
        const HomeRect tile = LogicalCard(slot, false);
        return {tile.x + tile.width / 2 - 20, tile.y + 8, 40, 46};
    }
    HomeRect LogicalRemove(int slot, bool clipboard) const {
        const HomeRect tile = LogicalCard(slot, clipboard);
        return {tile.Right() - 4 - kBubble, clipboard ? tile.Bottom() - 4 - kBubble : tile.y + 3, kBubble, kBubble};
    }
    HomeRect Px(HomeRect r) const { return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; }
    float width_, height_, header_, scale_;
};
}
