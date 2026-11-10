#pragma once

#include <d2d1_1.h>
#include <dcomp.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <string_view>

#include "features/home/home_layout.h"

namespace noitch {

// One Direct2D context and DirectWrite factory shared by the header and the usage page, so a second
// page costs a surface, not a second device. Created when the island first opens, released with
// the pager. Home keeps its own, as it did before pages existed.
class Painter {
public:
    HRESULT Create(IDXGIDevice* dxgi);
    void Release();
    bool Exists() const { return context_ != nullptr; }

    HRESULT MakeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment, IDWriteTextFormat** format);

    // Draws into `surface`, cleared, between Begin and End. Pixels are the unit throughout.
    HRESULT Begin(IDCompositionSurface* surface, float width, float height);
    HRESULT End(IDCompositionSurface* surface);

    void Text(std::string_view utf8, IDWriteTextFormat* format, const HomeRect& rect, D2D1_COLOR_F color);
    void Glyph(const wchar_t* glyph, IDWriteTextFormat* format, const HomeRect& rect, D2D1_COLOR_F color);
    void Fill(const HomeRect& rect, float radius, D2D1_COLOR_F color);

private:
    Microsoft::WRL::ComPtr<ID2D1Factory1> factory_;
    Microsoft::WRL::ComPtr<ID2D1Device> device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
};

}  // namespace noitch
