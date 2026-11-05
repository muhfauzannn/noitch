#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

#include "features/now_playing/media_session_source.h"
#include "home_layout.h"
#include "home_model.h"
#include "island/compositor.h"

namespace noitch {

// Draws Home into one surface with Direct2D and DirectWrite. It decides nothing, and it draws only
// when a value changes: never per frame. The surface sits inside the pill's clip, so the transition
// reveals it in DWM and collapsing hides it, with no fade of its own. Direct2D is delay-loaded, so
// none of this costs anything until the island first opens.
class HomeScene {
public:
    HRESULT Create(Compositor& compositor, const HomeLayout& layout, float left, float scale);
    void Release();
    bool Exists() const { return surface_ != nullptr; }

    void SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t tint);
    HRESULT Paint(const HomeModel& model, HomeControl hovered, bool isPressed);

private:
    HRESULT CreateFormats();
    void PaintText(const std::string& text, IDWriteTextFormat* format, const HomeRect& rect, float opacity);
    void PaintIcon(const wchar_t* glyph, HomeControl control, float opacity, bool isLarge);

    Compositor* compositor_ = nullptr;
    HomeLayout layout_;
    float scale_ = 1;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> factory_;
    Microsoft::WRL::ComPtr<ID2D1Device> device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> artistFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> lyricFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> timeFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> remainingFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> panelFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> panelCurrentFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> largeIconFormat_;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> artworkBrush_;
    uint32_t tint_ = 0xFFFFFFFF;
};

}  // namespace noitch
