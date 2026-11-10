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

// Draws Home into one surface with Direct2D and DirectWrite: a wash of the artwork's colour, the
// artwork, title and artist, the line being sung and the next one, the slider and the transport. It
// decides nothing, and it draws only when a value changes: never per frame. One surface means a
// lyric change redraws in place; the slide macOS does would need a visual per line. Direct2D is
// delay-loaded, so none of this costs anything until the island first opens.
class HomeScene {
public:
    HRESULT Create(Compositor& compositor, const HomeLayout& layout, float left, float scale);
    void Release();
    bool Exists() const { return surface_ != nullptr; }

    // `barTint` colours the progress bar and `glow` the wash behind the page, as BGRA; 0xFFFFFFFF is
    // white and none.
    void SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t barTint, uint32_t glow);
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
    Microsoft::WRL::ComPtr<IDWriteTextFormat> nextLyricFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> largeIconFormat_;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> artworkBrush_;
    uint32_t tint_ = 0xFFFFFFFF;
    uint32_t glow_ = 0xFFFFFFFF;
};

}  // namespace noitch
