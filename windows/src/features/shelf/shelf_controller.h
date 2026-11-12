#pragma once
#include "shelf_drag.h"
#include "shelf_scene.h"
namespace noitch {
class ShelfController {
public:
    static constexpr UINT_PTR kReleaseTimer = 21;
    void Initialize(HWND window, Compositor* compositor);
    void Stop();
    void SetEnabled(bool enabled);
    void PageWillShow(Painter& painter, ShelfLayout layout, float left);
    void PageWillHide();
    void DeviceLost() { scene_.Release(); }
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void PointerMoved(float x, float y);
    void PointerDown(float x, float y);
    void PointerUp(float x, float y);
    void Scrolled(double delta);
    bool IsInteracting() const { return dragging_ || targeting_; }
    std::function<void()> onOpen;
private:
    void Render();
    std::vector<ShelfItem> Items() const;
    void Save() { source_.Save(files_, clipboardItems_); }
    HWND window_ = nullptr;
    Compositor* compositor_ = nullptr;
    ShelfSource source_;
    ShelfScene scene_;
    Microsoft::WRL::ComPtr<ShelfDropTarget> drop_;
    bool registeredDrop_ = false;
    bool enabled_ = true;
    bool wantsRecording_ = true;
    bool clipboard_ = false;
    bool shown_ = false;
    bool dragging_ = false;
    bool targeting_ = false;
    bool pressed_ = false;
    float pressX_ = 0, pressY_ = 0;
    int hover_ = -1;
    size_t offset_ = 0;
    std::string message_;
    std::vector<ShelfItem> files_, clipboardItems_;
    std::vector<std::pair<std::string, ShelfThumbnail>> thumbnails_;
};
}
