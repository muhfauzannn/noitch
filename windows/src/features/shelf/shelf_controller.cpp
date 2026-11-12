#include "shelf_controller.h"
#include <cmath>
namespace noitch {
void ShelfController::Initialize(HWND window, Compositor* compositor) {
    window_ = window; compositor_ = compositor;
    source_.Initialize(window); wantsRecording_ = source_.Recording(); source_.Load(files_, clipboardItems_);
    drop_ = Microsoft::WRL::Make<ShelfDropTarget>();
    drop_->targeting = [this](bool entered) {
        targeting_ = entered && enabled_;
        if (targeting_ && onOpen) onOpen();
        Render();
    };
    drop_->dropped = [this](std::vector<std::string> paths) {
        if (!enabled_) return false;
        bool added = false;
        for (auto path = paths.rbegin(); path != paths.rend(); ++path) added = InsertShelfItem(files_, source_.Files({*path})) || added;
        if (added) {
            clipboard_ = false; offset_ = 0; message_ = "Added to shelf"; Save(); Render();
        } else { message_ = "Shelf full: unpin or remove something"; Render(); }
        return added;
    };
    registeredDrop_ = SUCCEEDED(RegisterDragDrop(window, drop_.Get()));
}
void ShelfController::SetEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    if (drop_) drop_->enabled = enabled;
    if (!enabled) targeting_ = false;
    if (enabled && wantsRecording_) source_.SetRecording(true);
    else source_.Stop();
}
void ShelfController::Stop() {
    source_.Stop();
    KillTimer(window_, kReleaseTimer);
    if (registeredDrop_) { RevokeDragDrop(window_); registeredDrop_ = false; }
    drop_.Reset(); scene_.Release();
}
std::vector<ShelfItem> ShelfController::Items() const {
    auto items = clipboard_ ? clipboardItems_ : files_;
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.pinned && !b.pinned; });
    return items;
}
void ShelfController::PageWillShow(Painter& painter, ShelfLayout layout, float left) {
    KillTimer(window_, kReleaseTimer); shown_ = true;
    scene_.Release();
    if (FAILED(scene_.Create(*compositor_, painter, layout, left))) scene_.Release();
    Render();
}
void ShelfController::PageWillHide() {
    shown_ = false; hover_ = -1; pressed_ = false; thumbnails_.clear();
    SetTimer(window_, kReleaseTimer, 500, nullptr);
}
void ShelfController::Render() {
    if (!shown_ || !scene_.Exists()) return;
    auto items = Items();
    const size_t page = static_cast<size_t>(ShelfLayout::PerPage(clipboard_));
    offset_ = std::min(offset_, (items.empty() ? 0 : (items.size() - 1) / page * page));
    const size_t total = items.size();
    items.erase(items.begin(), items.begin() + offset_);
    if (items.size() > page) items.resize(page);
    std::vector<ShelfThumbnail> previews;
    std::vector<std::pair<std::string, ShelfThumbnail>> next;
    for (const auto& item : items) {
        const auto found = std::find_if(thumbnails_.begin(), thumbnails_.end(), [&](const auto& cached) { return cached.first == item.id; });
        ShelfThumbnail preview = found != thumbnails_.end() ? found->second : item.kind == ShelfKind::Image ? DecodeShelfThumbnail(source_.Payload(item)) : ShelfThumbnail{};
        previews.push_back(preview);
        next.emplace_back(item.id, std::move(preview));
    }
    thumbnails_ = std::move(next);
    scene_.Paint(items, previews, clipboard_, source_.Recording(), offset_, total, hover_, message_, targeting_);
}
bool ShelfController::HandleMessage(UINT message, WPARAM wparam, LPARAM) {
    if (message == WM_CLIPBOARDUPDATE) {
        ShelfItem item;
        if (source_.Capture(&item) && InsertShelfItem(clipboardItems_, std::move(item))) {
            if (clipboard_) offset_ = 0;
            Save(); Render();
        }
        return true;
    }
    if (message == WM_TIMER && wparam == kReleaseTimer) { KillTimer(window_, kReleaseTimer); if (!shown_) scene_.Release(); return true; }
    return false;
}
void ShelfController::PointerMoved(float x, float y) {
    if (!scene_.Exists()) return;
    if (pressed_ && std::hypot(x - pressX_, y - pressY_) > 6 * scene_.Layout().Scale()) {
        const auto items = Items();
        for (int slot = 0; slot < ShelfLayout::PerPage(clipboard_); ++slot) {
            if (scene_.Layout().Card(slot, clipboard_).Contains(pressX_, pressY_) && offset_ + slot < items.size()) {
                const auto item = items[offset_ + slot];
                pressed_ = false; dragging_ = true;
                if (GetCapture() == window_) ReleaseCapture();
                const HRESULT hr = DragShelfItem(item, source_);
                dragging_ = false;
                message_ = hr == DRAGDROP_S_DROP ? "Sent to the other app" : "Drag ended";
                Render(); return;
            }
        }
    }
    int next = scene_.Layout().SlotAt(x, y, clipboard_);
    if (next >= 0 && offset_ + static_cast<size_t>(next) >= Items().size()) next = -1;
    if (next != hover_) { hover_ = next; Render(); }
}
void ShelfController::PointerDown(float x, float y) { pressed_ = true; pressX_ = x; pressY_ = y; message_.clear(); }
void ShelfController::PointerUp(float x, float y) {
    if (!pressed_ || !scene_.Exists()) return;
    pressed_ = false;
    const auto& layout = scene_.Layout();
    if (std::hypot(x - pressX_, y - pressY_) >= 6 * layout.Scale()) return;
    const size_t page = static_cast<size_t>(ShelfLayout::PerPage(clipboard_));
    for (int i = 0; i < 6; ++i) if ((i != 2 || clipboard_) && layout.Button(i).Contains(x, y) && layout.Button(i).Contains(pressX_, pressY_)) {
        auto& collection = clipboard_ ? clipboardItems_ : files_;
        switch (i) {
            case 0: clipboard_ = false; offset_ = 0; break;
            case 1: {
                clipboard_ = true; offset_ = 0;
                ShelfItem item;
                if (source_.Recording() && source_.Capture(&item, true) && InsertShelfItem(clipboardItems_, std::move(item))) Save();
                break;
            }
            case 2: wantsRecording_ = !wantsRecording_; source_.SetRecording(wantsRecording_ && enabled_); break;
            case 3: collection.erase(std::remove_if(collection.begin(), collection.end(), [](const auto& item) { return !item.pinned; }), collection.end()); Save(); break;
            case 4: offset_ = offset_ >= page ? offset_ - page : 0; break;
            case 5: if (offset_ + page < collection.size()) offset_ += page; break;
        }
        Render(); return;
    }
    const auto items = Items();
    const int slot = layout.SlotAt(x, y, clipboard_);
    if (slot < 0 || layout.SlotAt(pressX_, pressY_, clipboard_) != slot || offset_ + slot >= items.size()) return;
    const auto item = items[offset_ + slot];
    // The bubbles only show on hover, but a press there always means them.
    const float slack = 3 * layout.Scale();
    const auto hit = [&](HomeRect r) { return HomeRect{r.x - slack, r.y - slack, r.width + slack * 2, r.height + slack * 2}.Contains(x, y); };
    const bool remove = hit(layout.RemoveBubble(slot, clipboard_));
    if (remove || hit(layout.PinBubble(slot, clipboard_))) {
        auto& values = clipboard_ ? clipboardItems_ : files_;
        const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.id == item.id; });
        if (found != values.end()) {
            if (remove) values.erase(found);
            else found->pinned = !found->pinned;
            Save();
        }
    } else message_ = source_.Copy(item) ? "Copied" : "No longer available";
    Render();
}
void ShelfController::Scrolled(double delta) {
    if (std::abs(delta) <= 1) return;
    const size_t page = static_cast<size_t>(ShelfLayout::PerPage(clipboard_));
    if (delta > 0) offset_ = offset_ >= page ? offset_ - page : 0;
    else if (offset_ + page < Items().size()) offset_ += page;
    Render();
}
}
