#include "painter.h"

#include <d2d1_1helper.h>

#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;

namespace noitch {

namespace {

D2D1_RECT_F ToD2D(const HomeRect& r) { return D2D1::RectF(r.x, r.y, r.Right(), r.Bottom()); }

}  // namespace

HRESULT Painter::Create(IDXGIDevice* dxgi) {
    HRESULT hr;
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return hr;
    if (FAILED(hr = factory_->CreateDevice(dxgi, &device_))) return hr;
    if (FAILED(hr = device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_))) return hr;
    // Pixels are the unit everywhere here, so Direct2D must not scale again.
    context_->SetDpi(96, 96);
    if (FAILED(hr = context_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_))) return hr;
    const D2D1_STROKE_STYLE_PROPERTIES round = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
    ComPtr<ID2D1StrokeStyle> stroke;
    if (FAILED(hr = factory_->CreateStrokeStyle(round, nullptr, 0, &stroke))) return hr;
    roundStroke_ = stroke;
    return DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
}

void Painter::Release() {
    writeFactory_.Reset();
    roundStroke_.Reset();
    brush_.Reset();
    context_.Reset();
    device_.Reset();
    factory_.Reset();
}

HRESULT Painter::MakeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment, IDWriteTextFormat** format) {
    HRESULT hr = writeFactory_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", format);
    if (FAILED(hr)) return hr;
    (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    (*format)->SetTextAlignment(alignment);
    (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(writeFactory_->CreateEllipsisTrimmingSign(*format, &ellipsis))) {
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        (*format)->SetTrimming(&trimming, ellipsis.Get());
    }
    return S_OK;
}

HRESULT Painter::Begin(IDCompositionSurface* surface, float width, float height) {
    ComPtr<IDXGISurface> dxgiSurface;
    POINT offset{};
    HRESULT hr = surface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurface), &offset);
    if (FAILED(hr)) return hr;
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> target;
    if (FAILED(hr = context_->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &properties, &target))) {
        surface->EndDraw();
        return hr;
    }
    context_->SetTarget(target.Get());
    context_->BeginDraw();
    // The surface can be a region of a shared atlas, so stay inside the area at `offset`.
    context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    context_->PushAxisAlignedClip(D2D1::RectF(0, 0, width, height), D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    return S_OK;
}

HRESULT Painter::End(IDCompositionSurface* surface) {
    context_->PopAxisAlignedClip();
    const HRESULT hr = context_->EndDraw();
    context_->SetTarget(nullptr);
    const HRESULT end = surface->EndDraw();
    return FAILED(hr) ? hr : end;
}

void Painter::Text(std::string_view utf8, IDWriteTextFormat* format, const HomeRect& rect, D2D1_COLOR_F color) {
    if (utf8.empty()) return;
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), count);
    brush_->SetColor(color);
    context_->DrawText(wide.c_str(), static_cast<UINT32>(wide.size()), format, ToD2D(rect), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Painter::Glyph(const wchar_t* glyph, IDWriteTextFormat* format, const HomeRect& rect, D2D1_COLOR_F color) {
    brush_->SetColor(color);
    context_->DrawText(glyph, 1, format, ToD2D(rect), brush_.Get());
}

void Painter::Fill(const HomeRect& rect, float radius, D2D1_COLOR_F color) {
    brush_->SetColor(color);
    context_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(rect), radius, radius), brush_.Get());
}

void Painter::Arc(D2D1_POINT_2F center, float radius, float width, double fraction, D2D1_COLOR_F color) {
    if (fraction <= 0) return;
    brush_->SetColor(color);
    if (fraction >= 0.999) {
        context_->DrawEllipse(D2D1::Ellipse(center, radius, radius), brush_.Get(), width);
        return;
    }
    // Zero is the top, and pixel y grows downward, so increasing angles run clockwise.
    const double angle = fraction * 2 * 3.14159265358979323846;
    const D2D1_POINT_2F start = D2D1::Point2F(center.x, center.y - radius);
    const D2D1_POINT_2F end = D2D1::Point2F(center.x + radius * static_cast<float>(std::sin(angle)), center.y - radius * static_cast<float>(std::cos(angle)));
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  fraction > 0.5 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    context_->DrawGeometry(path.Get(), brush_.Get(), width, roundStroke_.Get());
}

void Painter::Dot(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color) {
    brush_->SetColor(color);
    context_->FillEllipse(D2D1::Ellipse(center, radius, radius), brush_.Get());
}

}  // namespace noitch
